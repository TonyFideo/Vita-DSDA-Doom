#include <stdlib.h>
#include <string.h>

#include <psp2/gxm.h>
#include <vitaGL.h>

#include "vita/vita_system.h"
#include "vita/vita_video.h"

#ifndef VITA_DISPLAY_WIDTH
#define VITA_DISPLAY_WIDTH 960
#endif

#ifndef VITA_DISPLAY_HEIGHT
#define VITA_DISPLAY_HEIGHT 544
#endif

#ifndef VITA_SOFTWARE_WIDTH
#define VITA_SOFTWARE_WIDTH 960
#endif

#ifndef VITA_SOFTWARE_HEIGHT
#define VITA_SOFTWARE_HEIGHT 544
#endif

static int vita_video_initialized;
static int vita_internal_width = VITA_SOFTWARE_WIDTH;
static int vita_internal_height = VITA_SOFTWARE_HEIGHT;
static int vita_texture_width;
static int vita_texture_height;
static GLuint vita_frame_texture;
static unsigned char *vita_upload_buffer;
static size_t vita_upload_buffer_size;

static void Vita_Set2DState(void)
{
  glViewport(0, 0, VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT, 0.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

int Vita_VideoInit(void)
{
  GLboolean resolution_fallback;

  if (vita_video_initialized)
    return 1;

  Vita_Log("[VITA] initializing VitaGL at %dx%d\n",
           VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  /*
   * Important: vitaGL's vglInitExtended() return value is NOT a generic
   * success/failure boolean. In the pinned vitaGL revision it returns
   * res_fallback from vglInitWithCustomSizes():
   *
   *   GL_FALSE = requested framebuffer resolution was accepted (normal)
   *   GL_TRUE  = resolution exceeded the display maximum and was clamped
   *
   * The old Vita port treated GL_FALSE as failure and therefore aborted every
   * normal 960x544 startup immediately after vitaGL had initialized.
   */
  resolution_fallback = vglInitExtended(
    0,
    VITA_DISPLAY_WIDTH,
    VITA_DISPLAY_HEIGHT,
    16 * 1024 * 1024,
    SCE_GXM_MULTISAMPLE_NONE
  );

  Vita_Log("[VITA] vglInitExtended returned %d (%s)\n",
           resolution_fallback,
           resolution_fallback ? "resolution fallback used" : "native resolution accepted");

  vglWaitVblankStart(GL_TRUE);
  Vita_Set2DState();

  vita_video_initialized = 1;
  Vita_Log("[VITA] VitaGL initialized at %dx%d\n",
           VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  return Vita_VideoResize(vita_internal_width, vita_internal_height);
}

void Vita_VideoShutdown(void)
{
  if (vita_frame_texture)
  {
    glDeleteTextures(1, &vita_frame_texture);
    vita_frame_texture = 0;
  }

  free(vita_upload_buffer);
  vita_upload_buffer = NULL;
  vita_upload_buffer_size = 0;

  /*
   * vitaGL currently owns process-lifetime GXM state. There is no public
   * shutdown call required by the presentation path; the process teardown
   * releases it after DSDA has destroyed its texture resources.
   */
  vita_video_initialized = 0;
}

int Vita_VideoResize(int width, int height)
{
  if (!vita_video_initialized || width <= 0 || height <= 0)
    return 0;

  if (vita_frame_texture)
  {
    glDeleteTextures(1, &vita_frame_texture);
    vita_frame_texture = 0;
  }

  glGenTextures(1, &vita_frame_texture);
  glBindTexture(GL_TEXTURE_2D, vita_frame_texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    width,
    height,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    NULL
  );

  vita_texture_width = width;
  vita_texture_height = height;

  Vita_Log("[VITA] software presentation texture: %dx%d RGBA8\n", width, height);
  return 1;
}

static const void *Vita_ContiguousPixels(
  const void *pixels,
  int pitch,
  int width,
  int height)
{
  const int row_bytes = width * 4;
  size_t required;
  int y;

  if (pitch == row_bytes)
    return pixels;

  required = (size_t)row_bytes * height;

  if (vita_upload_buffer_size < required)
  {
    unsigned char *new_buffer = realloc(vita_upload_buffer, required);

    if (!new_buffer)
      return NULL;

    vita_upload_buffer = new_buffer;
    vita_upload_buffer_size = required;
  }

  for (y = 0; y < height; ++y)
  {
    memcpy(
      vita_upload_buffer + (size_t)y * row_bytes,
      (const unsigned char *)pixels + (size_t)y * pitch,
      row_bytes
    );
  }

  return vita_upload_buffer;
}

void Vita_VideoPresent(const void *rgba_pixels, int pitch, int width, int height)
{
  const void *upload_pixels;
  float scale;
  float dst_width;
  float dst_height;
  float x0;
  float y0;
  GLfloat vertices[8];
  static const GLfloat texcoords[8] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
  };

  if (!vita_video_initialized || !rgba_pixels || width <= 0 || height <= 0)
    return;

  if (width != vita_texture_width || height != vita_texture_height)
    if (!Vita_VideoResize(width, height))
      return;

  upload_pixels = Vita_ContiguousPixels(rgba_pixels, pitch, width, height);
  if (!upload_pixels)
    return;

  glBindTexture(GL_TEXTURE_2D, vita_frame_texture);
  glTexSubImage2D(
    GL_TEXTURE_2D,
    0,
    0,
    0,
    width,
    height,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    upload_pixels
  );

  Vita_Set2DState();
  glClear(GL_COLOR_BUFFER_BIT);

  scale = (float)VITA_DISPLAY_WIDTH / (float)width;
  if ((float)height * scale > (float)VITA_DISPLAY_HEIGHT)
    scale = (float)VITA_DISPLAY_HEIGHT / (float)height;

  dst_width = (float)width * scale;
  dst_height = (float)height * scale;
  x0 = ((float)VITA_DISPLAY_WIDTH - dst_width) * 0.5f;
  y0 = ((float)VITA_DISPLAY_HEIGHT - dst_height) * 0.5f;

  vertices[0] = x0;
  vertices[1] = y0;
  vertices[2] = x0 + dst_width;
  vertices[3] = y0;
  vertices[4] = x0;
  vertices[5] = y0 + dst_height;
  vertices[6] = x0 + dst_width;
  vertices[7] = y0 + dst_height;

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glVertexPointer(2, GL_FLOAT, 0, vertices);
  glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  vglSwapBuffers(GL_FALSE);
}

int Vita_VideoInternalWidth(void)
{
  return vita_internal_width;
}

int Vita_VideoInternalHeight(void)
{
  return vita_internal_height;
}

void Vita_VideoSetInternalResolution(int width, int height)
{
  if (width <= 0 || height <= 0)
    return;

  vita_internal_width = width;
  vita_internal_height = height;
}
