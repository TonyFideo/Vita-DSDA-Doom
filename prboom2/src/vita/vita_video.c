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
