#include <stdlib.h>
#include <string.h>

#include <psp2/gxm.h>
#include <vitaGL.h>

#include "vita/vita_launcher.h"
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

/*
 * The pinned vitaGL keeps texture storage alive for four frames after use.
 * Rewriting the same texture sooner triggers its safe copy-on-write path,
 * which allocates and copies the whole texture. Five P8 textures let each
 * slot age past that window before reuse, avoiding allocator/copy churn while
 * remaining within vitaGL's own lifetime rules.
 */
#define VITA_P8_TEXTURE_RING 5

static int vita_video_initialized;
static int vita_internal_width = VITA_SOFTWARE_WIDTH;
static int vita_internal_height = VITA_SOFTWARE_HEIGHT;
static int vita_texture_width;
static int vita_texture_height;
static GLuint vita_frame_textures[VITA_P8_TEXTURE_RING];
static int vita_frame_texture_index;
static unsigned char vita_palette_rgba[256 * 4];
static void *vita_palette_gpu;
static int vita_palette_dirty;
static int vita_frame_presented;
static int vita_present_state_ready;
static int vita_launcher_framebuffer_released;

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

  vita_launcher_framebuffer_released = 0;

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

  /*
   * Do not submit viewport/depth/cull state before the first GXM scene.
   * The first software-present frame opens the scene with glClear(), then
   * installs the persistent 2D state used by later direct full-screen draws.
   */

  /*
   * P8 presentation keeps Doom's native indexed framebuffer all the way to
   * the GPU. vitaGL maps its allocator memory for GXM, so a small aligned
   * palette buffer can be bound directly to the P8 texture.
   */
  vita_palette_gpu = vglMemalign(SCE_GXM_PALETTE_ALIGNMENT, sizeof(vita_palette_rgba));
  if (!vita_palette_gpu)
  {
    Vita_Log("[VITA] failed to allocate P8 palette buffer\n");
    return 0;
  }

  memset(vita_palette_rgba, 0, sizeof(vita_palette_rgba));
  {
    int i;
    for (i = 0; i < 256; ++i)
      vita_palette_rgba[i * 4 + 3] = 255;
  }
  memcpy(vita_palette_gpu, vita_palette_rgba, sizeof(vita_palette_rgba));
  vita_palette_dirty = 0;
  vita_frame_presented = 0;

  vita_video_initialized = 1;
  Vita_Log("[VITA] VitaGL initialized at %dx%d\n",
           VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  return Vita_VideoResize(vita_internal_width, vita_internal_height);
}

void Vita_VideoShutdown(void)
{
  if (vita_frame_textures[0])
  {
    glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
    memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
  }
  vita_frame_texture_index = 0;

  if (vita_palette_gpu)
  {
    vglFree(vita_palette_gpu);
    vita_palette_gpu = NULL;
  }

  vita_palette_dirty = 0;
  vita_frame_presented = 0;
  vita_present_state_ready = 0;

  /*
   * vitaGL currently owns process-lifetime GXM state. There is no public
   * shutdown call required by the presentation path; the process teardown
   * releases it after DSDA has destroyed its texture resources.
   */
  vita_video_initialized = 0;
}

int Vita_VideoResize(int width, int height)
{
  int i;

  if (!vita_video_initialized || width <= 0 || height <= 0)
    return 0;

  if (vita_frame_textures[0])
  {
    glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
    memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
  }

  glGenTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);

  for (i = 0; i < VITA_P8_TEXTURE_RING; ++i)
  {
    SceGxmTexture *gxm_texture;

    glBindTexture(GL_TEXTURE_2D, vita_frame_textures[i]);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_COLOR_INDEX8_EXT,
      width,
      height,
      0,
      GL_RED,
      GL_UNSIGNED_BYTE,
      NULL
    );

    gxm_texture = vglGetGxmTexture(GL_TEXTURE_2D);
    if (!gxm_texture ||
        sceGxmTextureSetPalette(gxm_texture, vita_palette_gpu) < 0)
    {
      Vita_Log(
        "[VITA] failed to initialize P8 presentation texture %d/%d\n",
        i + 1,
        VITA_P8_TEXTURE_RING
      );
      glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
      memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
      return 0;
    }
  }

  vita_frame_texture_index = 0;
  vita_texture_width = width;
  vita_texture_height = height;
  vita_present_state_ready = 0;

  Vita_Log(
    "[VITA] software presentation textures: %dx%d P8 direct, ring=%d\n",
    width,
    height,
    VITA_P8_TEXTURE_RING
  );
  return 1;
}

void Vita_VideoSetPalette(const void *rgba_palette)
{
  if (!rgba_palette)
    return;

  memcpy(vita_palette_rgba, rgba_palette, sizeof(vita_palette_rgba));
  vita_palette_dirty = 1;
}

static void Vita_ApplyPendingPalette(void)
{
  if (!vita_palette_dirty || !vita_palette_gpu || !vita_frame_textures[0])
    return;

  /*
   * All five P8 textures point at this same palette allocation. Palette
   * changes are rare (damage/bonus/gamma); wait for queued frames only on
   * those changes, then update the shared table once.
   */
  if (vita_frame_presented)
    sceGxmDisplayQueueFinish();

  memcpy(vita_palette_gpu, vita_palette_rgba, sizeof(vita_palette_rgba));
  vita_palette_dirty = 0;
}

void Vita_VideoPresent(const void *indexed_pixels, int pitch, int width, int height)
{
  float scale;
  float dst_width;
  float dst_height;
  float x0;
  float y0;
  int covers_display;
  GLfloat vertices[8];
  static const GLfloat texcoords[8] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
  };

  if (!vita_video_initialized || !indexed_pixels || width <= 0 || height <= 0)
    return;

  if (width != vita_texture_width || height != vita_texture_height)
    if (!Vita_VideoResize(width, height))
      return;

  Vita_ApplyPendingPalette();

  glBindTexture(
    GL_TEXTURE_2D,
    vita_frame_textures[vita_frame_texture_index]
  );

  /*
   * Each ring slot is reused only after five swaps. In the pinned vitaGL
   * revision that is beyond FRAME_PURGE_FREQ (4), so glTexSubImage2D can
   * update the texture in place instead of entering texture copy-on-write.
   *
   * SDL may pad an 8-bit surface row. vitaGL honors GL_UNPACK_ROW_LENGTH,
   * allowing us to upload straight from Doom's framebuffer with no staging
   * copy or 8->32-bit conversion.
   */
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
  glTexSubImage2D(
    GL_TEXTURE_2D,
    0,
    0,
    0,
    width,
    height,
    GL_RED,
    GL_UNSIGNED_BYTE,
    indexed_pixels
  );
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  scale = (float)VITA_DISPLAY_WIDTH / (float)width;
  if ((float)height * scale > (float)VITA_DISPLAY_HEIGHT)
    scale = (float)VITA_DISPLAY_HEIGHT / (float)height;

  dst_width = (float)width * scale;
  dst_height = (float)height * scale;
  x0 = ((float)VITA_DISPLAY_WIDTH - dst_width) * 0.5f;
  y0 = ((float)VITA_DISPLAY_HEIGHT - dst_height) * 0.5f;

  /*
   * If the source has the display's exact aspect ratio, the textured quad
   * overwrites every color pixel. Clearing the 960x544 target first is then
   * entirely redundant. Keep one clear when entering/reconfiguring software
   * presentation so a valid GXM scene exists before Vita_Set2DState() touches
   * viewport/cull/depth state. Thereafter glDrawArrays() opens the scene on
   * full-screen frames itself; letterboxed modes still clear to black.
   */
  covers_display =
    width * VITA_DISPLAY_HEIGHT == height * VITA_DISPLAY_WIDTH;

  if (!vita_present_state_ready)
  {
    glClear(GL_COLOR_BUFFER_BIT);
    Vita_Set2DState();
    vita_present_state_ready = 1;
  }
  else if (!covers_display)
  {
    glClear(GL_COLOR_BUFFER_BIT);
  }

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
  vita_frame_presented = 1;
  vita_frame_texture_index =
    (vita_frame_texture_index + 1) % VITA_P8_TEXTURE_RING;

  if (!vita_launcher_framebuffer_released)
  {
    /*
     * vglSwapBuffers queues the new GXM surface asynchronously. Wait until the
     * display callback has installed it before freeing the launcher's CDRAM;
     * otherwise the display can still read the released 0x60000000 block.
     */
    sceGxmDisplayQueueFinish();
    Vita_LauncherReleaseFramebuffer();
    vita_launcher_framebuffer_released = 1;
  }
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
