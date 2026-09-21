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
static void *vita_frame_pixels[VITA_P8_TEXTURE_RING];
static void *vita_frame_palettes[VITA_P8_TEXTURE_RING];
static unsigned int vita_palette_slot_generation[VITA_P8_TEXTURE_RING];
static int vita_frame_texture_index;
static int vita_frame_pitch;
static size_t vita_frame_size;
static unsigned char vita_palette_rgba[256 * 4];
static unsigned int vita_palette_generation = 1;
static int vita_frame_presented;
static int vita_present_state_ready;
static GLfloat *vita_present_vertices;
static GLfloat *vita_present_texcoords;
static uint16_t *vita_present_indices;
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


static void Vita_UpdatePersistentQuad(int width, int height)
{
  float scale;
  float dst_width;
  float dst_height;
  float x0;
  float y0;

  if (!vita_present_vertices || width <= 0 || height <= 0)
    return;

  scale = (float)VITA_DISPLAY_WIDTH / (float)width;
  if ((float)height * scale > (float)VITA_DISPLAY_HEIGHT)
    scale = (float)VITA_DISPLAY_HEIGHT / (float)height;

  dst_width = (float)width * scale;
  dst_height = (float)height * scale;
  x0 = ((float)VITA_DISPLAY_WIDTH - dst_width) * 0.5f;
  y0 = ((float)VITA_DISPLAY_HEIGHT - dst_height) * 0.5f;

  vita_present_vertices[0] = x0;
  vita_present_vertices[1] = y0;
  vita_present_vertices[2] = x0 + dst_width;
  vita_present_vertices[3] = y0;
  vita_present_vertices[4] = x0;
  vita_present_vertices[5] = y0 + dst_height;
  vita_present_vertices[6] = x0 + dst_width;
  vita_present_vertices[7] = y0 + dst_height;
}

static void Vita_BindPersistentPresenter(void)
{
  vglVertexPointerMapped(2, vita_present_vertices);
  vglTexCoordPointerMapped(vita_present_texcoords);
  vglIndexPointerMapped(vita_present_indices);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);

  /*
   * The indexed P8 texture and its palette already provide the final fragment
   * color. GL_REPLACE removes the fixed-function color modulation stage.
   */
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
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
   * Keep one CPU copy of the active PLAYPAL. Each ring slot gets its own
   * GPU-visible palette in Vita_VideoResize(), so palette changes never need
   * to stall the display queue.
   */
  memset(vita_palette_rgba, 0, sizeof(vita_palette_rgba));
  {
    int i;
    for (i = 0; i < 256; ++i)
      vita_palette_rgba[i * 4 + 3] = 255;
  }
  vita_palette_generation = 1;
  vita_frame_presented = 0;

  vita_present_vertices = (GLfloat *)vglMemalign(16, sizeof(GLfloat) * 8);
  vita_present_texcoords = (GLfloat *)vglMemalign(16, sizeof(GLfloat) * 8);
  vita_present_indices = (uint16_t *)vglMemalign(16, sizeof(uint16_t) * 4);

  if (!vita_present_vertices || !vita_present_texcoords || !vita_present_indices)
  {
    Vita_Log("[VITA] failed to allocate persistent presenter arrays\n");

    if (vita_present_vertices)
      vglFree(vita_present_vertices);
    if (vita_present_texcoords)
      vglFree(vita_present_texcoords);
    if (vita_present_indices)
      vglFree(vita_present_indices);

    vita_present_vertices = NULL;
    vita_present_texcoords = NULL;
    vita_present_indices = NULL;
    return 0;
  }

  vita_present_texcoords[0] = 0.0f;
  vita_present_texcoords[1] = 0.0f;
  vita_present_texcoords[2] = 1.0f;
  vita_present_texcoords[3] = 0.0f;
  vita_present_texcoords[4] = 0.0f;
  vita_present_texcoords[5] = 1.0f;
  vita_present_texcoords[6] = 1.0f;
  vita_present_texcoords[7] = 1.0f;

  vita_present_indices[0] = 0;
  vita_present_indices[1] = 1;
  vita_present_indices[2] = 2;
  vita_present_indices[3] = 3;

  vita_video_initialized = 1;
  Vita_Log("[VITA] VitaGL initialized at %dx%d\n",
           VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  return Vita_VideoResize(vita_internal_width, vita_internal_height);
}

void Vita_VideoShutdown(void)
{
  int i;

  if (vita_frame_presented)
    sceGxmDisplayQueueFinish();

  if (vita_frame_textures[0])
  {
    glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
    memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
  }

  /*
   * Texture deletion owns/frees vita_frame_pixels[] because each GL texture's
   * internal data pointer was overloaded with that allocation. Palettes are
   * external to vitaGL texture ownership, so release them explicitly.
   */
  memset(vita_frame_pixels, 0, sizeof(vita_frame_pixels));
  for (i = 0; i < VITA_P8_TEXTURE_RING; ++i)
  {
    if (vita_frame_palettes[i])
    {
      vglFree(vita_frame_palettes[i]);
      vita_frame_palettes[i] = NULL;
    }
    vita_palette_slot_generation[i] = 0;
  }

  if (vita_present_vertices)
  {
    vglFree(vita_present_vertices);
    vita_present_vertices = NULL;
  }

  if (vita_present_texcoords)
  {
    vglFree(vita_present_texcoords);
    vita_present_texcoords = NULL;
  }

  if (vita_present_indices)
  {
    vglFree(vita_present_indices);
    vita_present_indices = NULL;
  }

  vita_frame_texture_index = 0;
  vita_frame_pitch = 0;
  vita_frame_size = 0;
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
  const int pitch = (width + 7) & ~7;
  const size_t frame_size = (size_t)pitch * (size_t)height;

  if (!vita_video_initialized || width <= 0 || height <= 0)
    return 0;

  if (vita_frame_presented)
    sceGxmDisplayQueueFinish();

  if (vita_frame_textures[0])
  {
    glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
    memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
    memset(vita_frame_pixels, 0, sizeof(vita_frame_pixels));
  }

  for (i = 0; i < VITA_P8_TEXTURE_RING; ++i)
  {
    if (vita_frame_palettes[i])
    {
      vglFree(vita_frame_palettes[i]);
      vita_frame_palettes[i] = NULL;
    }
    vita_palette_slot_generation[i] = 0;
  }

  glGenTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);

  for (i = 0; i < VITA_P8_TEXTURE_RING; ++i)
  {
    SceGxmTexture *gxm_texture;
    void *old_texture_data;
    void *frame_pixels;
    void *palette;

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
    old_texture_data = vglGetTexDataPointer(GL_TEXTURE_2D);

    /*
     * vglMemalign() prefers the newlib heap / normal user RAM, and vitaGL maps
     * that heap for GXM during init. This gives the software renderer cached
     * CPU-friendly memory that the GPU can sample directly.
     */
    frame_pixels = vglMemalign(SCE_GXM_TEXTURE_ALIGNMENT, (uint32_t)frame_size);
    palette = vglMemalign(
      SCE_GXM_PALETTE_ALIGNMENT,
      (uint32_t)sizeof(vita_palette_rgba)
    );

    if (!gxm_texture || !old_texture_data || !frame_pixels || !palette)
    {
      if (frame_pixels)
        vglFree(frame_pixels);
      if (palette)
        vglFree(palette);

      Vita_Log(
        "[VITA] failed to allocate zero-copy P8 slot %d/%d\n",
        i + 1,
        VITA_P8_TEXTURE_RING
      );

      glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
      memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
      memset(vita_frame_pixels, 0, sizeof(vita_frame_pixels));

      while (--i >= 0)
      {
        if (vita_frame_palettes[i])
        {
          vglFree(vita_frame_palettes[i]);
          vita_frame_palettes[i] = NULL;
        }
      }
      return 0;
    }

    memset(frame_pixels, 0, frame_size);
    memcpy(palette, vita_palette_rgba, sizeof(vita_palette_rgba));

    /*
     * Replace vitaGL's texture allocation with the exact RAM block DSDA will
     * rasterize into. The GXM texture descriptor and vitaGL's ownership pointer
     * must both be updated so draw and eventual deletion refer to the same data.
     */
    vglFree(old_texture_data);
    sceGxmTextureSetData(gxm_texture, frame_pixels);
    vglOverloadTexDataPointer(GL_TEXTURE_2D, frame_pixels);

    if (sceGxmTextureSetPalette(gxm_texture, palette) < 0)
    {
      Vita_Log(
        "[VITA] failed to bind P8 palette for zero-copy slot %d/%d\n",
        i + 1,
        VITA_P8_TEXTURE_RING
      );
      glDeleteTextures(VITA_P8_TEXTURE_RING, vita_frame_textures);
      memset(vita_frame_textures, 0, sizeof(vita_frame_textures));
      memset(vita_frame_pixels, 0, sizeof(vita_frame_pixels));
      vglFree(palette);

      while (--i >= 0)
      {
        if (vita_frame_palettes[i])
        {
          vglFree(vita_frame_palettes[i]);
          vita_frame_palettes[i] = NULL;
        }
      }
      return 0;
    }

    vita_frame_pixels[i] = frame_pixels;
    vita_frame_palettes[i] = palette;
    vita_palette_slot_generation[i] = vita_palette_generation;
  }

  vita_frame_texture_index = 0;
  vita_frame_pitch = pitch;
  vita_frame_size = frame_size;
  vita_texture_width = width;
  vita_texture_height = height;
  vita_present_state_ready = 0;
  vita_frame_presented = 0;
  Vita_UpdatePersistentQuad(width, height);

  Vita_Log(
    "[VITA] software presentation: %dx%d P8 zero-copy, pitch=%d bytes=%u ring=%d palettes=%d mapped_quad=1\n",
    width,
    height,
    pitch,
    (unsigned int)frame_size,
    VITA_P8_TEXTURE_RING,
    VITA_P8_TEXTURE_RING
  );
  return 1;
}

void Vita_VideoSetPalette(const void *rgba_palette)
{
  if (!rgba_palette)
    return;

  memcpy(vita_palette_rgba, rgba_palette, sizeof(vita_palette_rgba));

  ++vita_palette_generation;
  if (!vita_palette_generation)
  {
    int i;
    vita_palette_generation = 1;
    for (i = 0; i < VITA_P8_TEXTURE_RING; ++i)
      vita_palette_slot_generation[i] = 0;
  }
}

static void Vita_ApplyCurrentPalette(void)
{
  const int slot = vita_frame_texture_index;

  if (!vita_frame_palettes[slot] ||
      vita_palette_slot_generation[slot] == vita_palette_generation)
    return;

  /*
   * The slot has aged through the five-entry ring before reuse, so updating
   * its private palette cannot race an in-flight frame. No display-queue stall
   * is needed when PLAYPAL changes.
   */
  memcpy(
    vita_frame_palettes[slot],
    vita_palette_rgba,
    sizeof(vita_palette_rgba)
  );
  vita_palette_slot_generation[slot] = vita_palette_generation;
}

void *Vita_VideoRenderBuffer(void)
{
  return vita_frame_pixels[vita_frame_texture_index];
}

int Vita_VideoRenderPitch(void)
{
  return vita_frame_pitch;
}

void Vita_VideoPresent(int width, int height)
{
  int covers_display;
  unsigned int vita_prof_start = 0;

  if (!vita_video_initialized ||
      !vita_frame_pixels[vita_frame_texture_index] ||
      width <= 0 || height <= 0)
    return;

  if (Vita_ProfileActive())
    vita_prof_start = Vita_ProfileTimestamp();

  if (width != vita_texture_width || height != vita_texture_height)
    if (!Vita_VideoResize(width, height))
      return;

  Vita_ApplyCurrentPalette();

  glBindTexture(
    GL_TEXTURE_2D,
    vita_frame_textures[vita_frame_texture_index]
  );

  /*
   * Zero-copy path: the software renderer already wrote directly into the
   * RAM block sampled by this P8 texture. The presentation quad is likewise
   * persistent GPU-mapped memory, so there is no per-frame vertex upload.
   */
  covers_display =
    width * VITA_DISPLAY_HEIGHT == height * VITA_DISPLAY_WIDTH;

  if (!vita_present_state_ready)
  {
    /*
     * Open the first GXM scene before touching fixed-function render state,
     * matching the launcher handoff sequence that is known to be stable.
     */
    glClear(GL_COLOR_BUFFER_BIT);
    Vita_Set2DState();
    Vita_BindPersistentPresenter();
    vita_present_state_ready = 1;
  }
  else if (!covers_display)
  {
    glClear(GL_COLOR_BUFFER_BIT);
  }

  vglDrawObjects(GL_TRIANGLE_STRIP, 4);

  vglSwapBuffers(GL_FALSE);
  vita_frame_presented = 1;
  vita_frame_texture_index =
    (vita_frame_texture_index + 1) % VITA_P8_TEXTURE_RING;

  if (!vita_launcher_framebuffer_released)
  {
    /*
     * vglSwapBuffers queues the new GXM surface asynchronously. Wait until the
     * display callback has installed it before freeing the launcher's CDRAM.
     */
    sceGxmDisplayQueueFinish();
    Vita_LauncherReleaseFramebuffer();
    vita_launcher_framebuffer_released = 1;
  }

  if (vita_prof_start)
    Vita_ProfileAdd(
      VITA_PROFILE_PRESENT,
      (unsigned int)(Vita_ProfileTimestamp() - vita_prof_start)
    );
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
