#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>

#include "vita/vita_launcher.h"
#include "vita/vita_system.h"
#include "vita/vita_video.h"

#include "dsda/args.h"
#include "textscreen/txt_main.h"
#include "textscreen/fonts/normal.h"

#ifndef VITA_DISPLAY_WIDTH
#define VITA_DISPLAY_WIDTH 960
#endif

#ifndef VITA_DISPLAY_HEIGHT
#define VITA_DISPLAY_HEIGHT 544
#endif

#define LAUNCHER_FONT_W 8
#define LAUNCHER_FONT_H 18
#define LAUNCHER_FB_ALIGNMENT (256 * 1024)

typedef struct
{
  const char *label;
  int width;
  int height;
} vita_resolution_option_t;

static const vita_resolution_option_t vita_resolution_options[] = {
  { "0,25 (240x136)", 240, 136 },
  { "320x200",        320, 200 },
  { "320x240",        320, 240 },
  { "0,50 (480x272)", 480, 272 },
  { "480x272",        480, 272 },
  { "640x400",        640, 400 },
  { "0,75 (720x408)", 720, 408 },
  { "640x480",        640, 480 },
  { "NATIVO (960x544)", 960, 544 },
};

static vita_launcher_renderer_t vita_launcher_renderer =
  VITA_LAUNCHER_RENDERER_SOFTWARE;

static SceUID launcher_fb_uid = -1;
static uint32_t *launcher_fb_pixels;

static size_t Vita_LauncherAlignUp(size_t value, size_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

static uint8_t Vita_LauncherColor(float value)
{
  if (value <= 0.0f)
    return 0;
  if (value >= 1.0f)
    return 255;
  return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t Vita_LauncherPackRGBA(
  uint8_t r,
  uint8_t g,
  uint8_t b,
  uint8_t a)
{
  /*
   * SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 is AABBGGRR as a 32-bit word.
   * On the Vita's little-endian CPU that is RGBA byte order in memory.
   */
  return ((uint32_t)a << 24) |
         ((uint32_t)b << 16) |
         ((uint32_t)g << 8) |
         (uint32_t)r;
}

static int Vita_LauncherInitFramebuffer(void)
{
  const size_t raw_size =
    (size_t)VITA_DISPLAY_WIDTH * VITA_DISPLAY_HEIGHT * sizeof(uint32_t);
  const size_t alloc_size =
    Vita_LauncherAlignUp(raw_size, LAUNCHER_FB_ALIGNMENT);
  SceDisplayFrameBuf fb;

  if (launcher_fb_pixels)
    return 1;

  launcher_fb_uid = sceKernelAllocMemBlock(
    "dsda_launcher_fb",
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
    (SceSize)alloc_size,
    NULL
  );
  if (launcher_fb_uid < 0)
  {
    Vita_Log(
      "[VITA] launcher: framebuffer allocation failed: 0x%08x\n",
      (unsigned int)launcher_fb_uid
    );
    launcher_fb_uid = -1;
    return 0;
  }

  if (sceKernelGetMemBlockBase(launcher_fb_uid, (void **)&launcher_fb_pixels) < 0 ||
      !launcher_fb_pixels)
  {
    Vita_Log("[VITA] launcher: unable to get framebuffer base\n");
    sceKernelFreeMemBlock(launcher_fb_uid);
    launcher_fb_uid = -1;
    launcher_fb_pixels = NULL;
    return 0;
  }

  memset(launcher_fb_pixels, 0, raw_size);
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  fb.base = launcher_fb_pixels;
  fb.pitch = VITA_DISPLAY_WIDTH;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = VITA_DISPLAY_WIDTH;
  fb.height = VITA_DISPLAY_HEIGHT;

  if (sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE) < 0)
  {
    Vita_Log("[VITA] launcher: sceDisplaySetFrameBuf failed\n");
    sceKernelFreeMemBlock(launcher_fb_uid);
    launcher_fb_uid = -1;
    launcher_fb_pixels = NULL;
    return 0;
  }

  Vita_Log(
    "[VITA] launcher framebuffer ready: %dx%d, %u bytes\n",
    VITA_DISPLAY_WIDTH,
    VITA_DISPLAY_HEIGHT,
    (unsigned int)alloc_size
  );
  return 1;
}

void Vita_LauncherReleaseFramebuffer(void)
{
  if (!launcher_fb_pixels)
    return;

  /*
   * This must only be called after another framebuffer has become the active
   * scanout. In the gameplay transition Vita_VideoPresent() waits for the
   * vitaGL display queue before releasing this CDRAM block.
   */
  launcher_fb_pixels = NULL;

  if (launcher_fb_uid >= 0)
  {
    sceKernelFreeMemBlock(launcher_fb_uid);
    launcher_fb_uid = -1;
  }

  Vita_Log("[VITA] launcher framebuffer released after VitaGL takeover\n");
}

static void Vita_LauncherClear(
  uint8_t r,
  uint8_t g,
  uint8_t b,
  uint8_t a)
{
  const uint32_t color = Vita_LauncherPackRGBA(r, g, b, a);
  const size_t count = (size_t)VITA_DISPLAY_WIDTH * VITA_DISPLAY_HEIGHT;
  size_t i;

  if (!launcher_fb_pixels)
    return;

  for (i = 0; i < count; ++i)
    launcher_fb_pixels[i] = color;
}

static void Vita_LauncherBlendPixel(
  int x,
  int y,
  uint8_t r,
  uint8_t g,
  uint8_t b,
  uint8_t a)
{
  uint32_t *dst;
  uint32_t packed;

  if (!launcher_fb_pixels ||
      x < 0 || x >= VITA_DISPLAY_WIDTH ||
      y < 0 || y >= VITA_DISPLAY_HEIGHT ||
      a == 0)
    return;

  dst = &launcher_fb_pixels[(size_t)y * VITA_DISPLAY_WIDTH + x];

  if (a == 255)
  {
    *dst = Vita_LauncherPackRGBA(r, g, b, 255);
    return;
  }

  packed = *dst;

  {
    const unsigned int inv = 255u - a;
    const unsigned int dr = packed & 0xffu;
    const unsigned int dg = (packed >> 8) & 0xffu;
    const unsigned int db = (packed >> 16) & 0xffu;
    const uint8_t out_r = (uint8_t)((r * a + dr * inv + 127u) / 255u);
    const uint8_t out_g = (uint8_t)((g * a + dg * inv + 127u) / 255u);
    const uint8_t out_b = (uint8_t)((b * a + db * inv + 127u) / 255u);

    *dst = Vita_LauncherPackRGBA(out_r, out_g, out_b, 255);
  }
}

static void Vita_LauncherDrawRect(
  float x,
  float y,
  float w,
  float h,
  float r,
  float g,
  float b,
  float a)
{
  int x0 = (int)x;
  int y0 = (int)y;
  int x1 = (int)(x + w + 0.5f);
  int y1 = (int)(y + h + 0.5f);
  int px;
  int py;
  const uint8_t cr = Vita_LauncherColor(r);
  const uint8_t cg = Vita_LauncherColor(g);
  const uint8_t cb = Vita_LauncherColor(b);
  const uint8_t ca = Vita_LauncherColor(a);

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > VITA_DISPLAY_WIDTH) x1 = VITA_DISPLAY_WIDTH;
  if (y1 > VITA_DISPLAY_HEIGHT) y1 = VITA_DISPLAY_HEIGHT;

  for (py = y0; py < y1; ++py)
  {
    for (px = x0; px < x1; ++px)
      Vita_LauncherBlendPixel(px, py, cr, cg, cb, ca);
  }
}

static void Vita_LauncherDrawText(
  float x,
  float y,
  float scale,
  const char *text,
  float r,
  float g,
  float b,
  float a)
{
  const uint8_t cr = Vita_LauncherColor(r);
  const uint8_t cg = Vita_LauncherColor(g);
  const uint8_t cb = Vita_LauncherColor(b);
  const uint8_t ca = Vita_LauncherColor(a);
  int cursor_x = (int)x;
  const int base_y = (int)y;
  int glyph_w = (int)(LAUNCHER_FONT_W * scale + 0.5f);
  int glyph_h = (int)(LAUNCHER_FONT_H * scale + 0.5f);

  if (!text || !launcher_fb_pixels)
    return;

  if (glyph_w < 1) glyph_w = 1;
  if (glyph_h < 1) glyph_h = 1;

  while (*text)
  {
    const unsigned char ch = (unsigned char)*text++;
    int dy;

    for (dy = 0; dy < glyph_h; ++dy)
    {
      const int sy = (dy * LAUNCHER_FONT_H) / glyph_h;
      const unsigned char row = normal_font.data[ch * LAUNCHER_FONT_H + sy];
      int dx;

      for (dx = 0; dx < glyph_w; ++dx)
      {
        const int sx = (dx * LAUNCHER_FONT_W) / glyph_w;

        if (row & (1u << sx))
          Vita_LauncherBlendPixel(cursor_x + dx, base_y + dy, cr, cg, cb, ca);
      }
    }

    cursor_x += glyph_w;
  }
}

static void Vita_LauncherUppercase(char *text)
{
  char *p = text;

  if (!p)
    return;

  while (*p)
  {
    *p = (char)toupper((unsigned char)*p);
    ++p;
  }
}

static void Vita_LauncherIWADLabel(int index, char *label, size_t label_size)
{
  const char *path = Vita_IWADPathAt(index);
  const char *name = Vita_IWADNameAt(index);
  char partition[5] = "----";

  if (!path || !name)
  {
    snprintf(label, label_size, "NO HAY IWADS");
    return;
  }

  if (strlen(path) >= 4)
  {
    memcpy(partition, path, 4);
    partition[4] = '\0';
  }

  snprintf(label, label_size, "%s %s", partition, name);
  Vita_LauncherUppercase(label);

  if (strlen(label) > 46)
  {
    label[43] = '.';
    label[44] = '.';
    label[45] = '.';
    label[46] = '\0';
  }
}

static void Vita_LauncherPWADLabel(int selector_index, char *label, size_t label_size)
{
  const char *path;
  const char *name;
  char partition[5] = "----";
  const int pwad_index = selector_index - 1;

  if (selector_index <= 0)
  {
    snprintf(label, label_size, "NINGUNO");
    return;
  }

  path = Vita_PWADPathAt(pwad_index);
  name = Vita_PWADNameAt(pwad_index);

  if (!path || !name)
  {
    snprintf(label, label_size, "NINGUNO");
    return;
  }

  if (strlen(path) >= 4)
  {
    memcpy(partition, path, 4);
    partition[4] = '\0';
  }

  snprintf(label, label_size, "%s %s", partition, name);
  Vita_LauncherUppercase(label);

  if (strlen(label) > 46)
  {
    label[43] = '.';
    label[44] = '.';
    label[45] = '.';
    label[46] = '\0';
  }
}

static void Vita_LauncherDrawRow(
  int row,
  int selected,
  const char *name,
  const char *value)
{
  const float y = 146.0f + row * 58.0f;

  if (selected)
  {
    Vita_LauncherDrawRect(
      80.0f, y - 11.0f, 800.0f, 48.0f,
      0.10f, 0.36f, 0.55f, 0.90f
    );
  }
  else
  {
    Vita_LauncherDrawRect(
      80.0f, y - 11.0f, 800.0f, 48.0f,
      0.08f, 0.08f, 0.10f, 0.82f
    );
  }

  Vita_LauncherDrawText(
    105.0f, y, 1.10f, name,
    selected ? 1.0f : 0.78f,
    selected ? 1.0f : 0.78f,
    selected ? 1.0f : 0.78f,
    1.0f
  );

  if (value)
  {
    Vita_LauncherDrawText(
      420.0f, y, 1.10f, "<",
      0.70f, 0.90f, 1.0f, 1.0f
    );
    Vita_LauncherDrawText(
      452.0f, y, 1.10f, value,
      1.0f, 1.0f, 1.0f, 1.0f
    );
    Vita_LauncherDrawText(
      820.0f, y, 1.10f, ">",
      0.70f, 0.90f, 1.0f, 1.0f
    );
  }
}

static void Vita_LauncherDraw(
  int selected_row,
  int resolution_index,
  int iwad_index,
  int pwad_selector_index,
  const char *status)
{
  char iwad_label[64];
  char pwad_label[64];
  const char *renderer_name =
    vita_launcher_renderer == VITA_LAUNCHER_RENDERER_SOFTWARE
      ? "SOFTWARE"
      : "VITAGL";

  Vita_LauncherIWADLabel(iwad_index, iwad_label, sizeof(iwad_label));
  Vita_LauncherPWADLabel(pwad_selector_index, pwad_label, sizeof(pwad_label));

  Vita_LauncherClear(4, 5, 7, 255);

  Vita_LauncherDrawRect(0.0f, 0.0f, 960.0f, 105.0f, 0.04f, 0.13f, 0.20f, 1.0f);
  Vita_LauncherDrawText(
    78.0f, 35.0f, 1.65f, "VITA-DSDA-DOOM",
    1.0f, 1.0f, 1.0f, 1.0f
  );
  Vita_LauncherDrawText(
    80.0f, 75.0f, 0.82f, "SELECTOR DE INICIO",
    0.55f, 0.82f, 1.0f, 1.0f
  );

  Vita_LauncherDrawRow(
    0,
    selected_row == 0,
    "MODO DE RENDERIZADO",
    renderer_name
  );

  Vita_LauncherDrawRow(
    1,
    selected_row == 1,
    "RESOLUCION",
    vita_resolution_options[resolution_index].label
  );

  Vita_LauncherDrawRow(
    2,
    selected_row == 2,
    "IWAD",
    iwad_label
  );

  Vita_LauncherDrawRow(
    3,
    selected_row == 3,
    "PWAD",
    pwad_label
  );

  Vita_LauncherDrawRow(
    4,
    selected_row == 4,
    "INICIAR JUEGO",
    NULL
  );

  if (selected_row == 4)
  {
    Vita_LauncherDrawText(
      680.0f, 378.0f, 1.10f, "[ X ]",
      0.70f, 0.90f, 1.0f, 1.0f
    );
  }

  if (vita_launcher_renderer == VITA_LAUNCHER_RENDERER_VITAGL)
  {
    Vita_LauncherDrawText(
      82.0f, 452.0f, 0.72f,
      "VITAGL: RENDERER PREPARADO PARA DESARROLLO FUTURO",
      1.0f, 0.72f, 0.30f, 1.0f
    );
  }

  if (status && *status)
  {
    Vita_LauncherDrawText(
      82.0f, 482.0f, 0.72f, status,
      1.0f, 0.45f, 0.40f, 1.0f
    );
  }
  else
  {
    Vita_LauncherDrawText(
      82.0f, 482.0f, 0.68f,
      "ARRIBA/ABAJO: OPCION   IZQ/DER: CAMBIAR   X: SELECCIONAR",
      0.72f, 0.72f, 0.76f, 1.0f
    );
  }

  sceDisplayWaitVblankStart();
}

static int Vita_LauncherWrap(int value, int count)
{
  if (count <= 0)
    return 0;

  while (value < 0)
    value += count;

  while (value >= count)
    value -= count;

  return value;
}

int Vita_LauncherRun(void)
{
  SceCtrlData pad;
  unsigned int previous_buttons = 0;
  const int resolution_count =
    (int)(sizeof(vita_resolution_options) / sizeof(vita_resolution_options[0]));
  int selected_row = 0;
  int resolution_index = resolution_count - 1;
  int iwad_count;
  int iwad_index;
  int pwad_count;
  int pwad_selector_index = 0;
  char status[96] = {0};

  Vita_RefreshIWADs();
  Vita_RefreshPWADs();
  iwad_count = Vita_IWADCount();
  iwad_index = Vita_SelectedIWADIndex();
  pwad_count = Vita_PWADCount();

  if (!Vita_LauncherInitFramebuffer())
  {
    Vita_Log("[VITA] launcher: direct framebuffer init failed\n");
    return 0;
  }

  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

  Vita_Log(
    "[VITA] launcher opened: %d IWAD(s), %d PWAD(s) available\n",
    iwad_count,
    pwad_count
  );

  for (;;)
  {
    unsigned int pressed = 0;

    memset(&pad, 0, sizeof(pad));

    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
    {
      pressed = pad.buttons & ~previous_buttons;
      previous_buttons = pad.buttons;
    }

    if (pressed & SCE_CTRL_UP)
    {
      selected_row = Vita_LauncherWrap(selected_row - 1, 5);
      status[0] = '\0';
    }

    if (pressed & SCE_CTRL_DOWN)
    {
      selected_row = Vita_LauncherWrap(selected_row + 1, 5);
      status[0] = '\0';
    }

    if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))
    {
      const int direction = (pressed & SCE_CTRL_RIGHT) ? 1 : -1;

      status[0] = '\0';

      switch (selected_row)
      {
        case 0:
          vita_launcher_renderer =
            vita_launcher_renderer == VITA_LAUNCHER_RENDERER_SOFTWARE
              ? VITA_LAUNCHER_RENDERER_VITAGL
              : VITA_LAUNCHER_RENDERER_SOFTWARE;
          break;

        case 1:
          resolution_index = Vita_LauncherWrap(
            resolution_index + direction,
            resolution_count
          );
          break;

        case 2:
          if (iwad_count > 0)
          {
            iwad_index = Vita_LauncherWrap(iwad_index + direction, iwad_count);
            Vita_SelectIWAD(iwad_index);
          }
          break;

        case 3:
          pwad_selector_index = Vita_LauncherWrap(
            pwad_selector_index + direction,
            pwad_count + 1
          );
          Vita_SelectPWAD(pwad_selector_index - 1);
          break;

        default:
          break;
      }
    }

    if (pressed & SCE_CTRL_CROSS)
    {
      if (selected_row == 4)
      {
        if (iwad_count <= 0)
        {
          snprintf(
            status,
            sizeof(status),
            "NO HAY IWADS EN ux0/uma0/ur0:/data/DSDA-Doom/IWADs"
          );
        }
        else if (vita_launcher_renderer == VITA_LAUNCHER_RENDERER_VITAGL)
        {
          snprintf(
            status,
            sizeof(status),
            "EL RENDERER VITAGL TODAVIA NO ESTA IMPLEMENTADO"
          );
        }
        else
        {
          const vita_resolution_option_t *resolution =
            &vita_resolution_options[resolution_index];

          const int pwad_index = pwad_selector_index - 1;
          const char *pwad_path = NULL;

          Vita_SelectIWAD(iwad_index);
          Vita_SelectPWAD(pwad_index);

          if (pwad_index >= 0)
          {
            pwad_path = Vita_PWADPathAt(pwad_index);
            if (pwad_path)
              dsda_AppendStringArg(dsda_arg_file, pwad_path);
          }

          Vita_VideoSetInternalResolution(
            resolution->width,
            resolution->height
          );

          Vita_Log(
            "[VITA] launcher start: renderer=software resolution=%dx%d IWAD=%s PWAD=%s\n",
            resolution->width,
            resolution->height,
            Vita_IWADPathAt(iwad_index),
            pwad_path ? pwad_path : "none"
          );

          Vita_Log("[VITA] launcher handoff: retaining framebuffer until first VitaGL flip\n");
          return 1;
        }
      }
    }

    if (pressed & SCE_CTRL_CIRCLE)
    {
      Vita_Log("[VITA] launcher exit requested\n");
      return 0;
    }

    Vita_LauncherDraw(
      selected_row,
      resolution_index,
      iwad_index,
      pwad_selector_index,
      status
    );
  }
}

vita_launcher_renderer_t Vita_LauncherRenderer(void)
{
  return vita_launcher_renderer;
}
