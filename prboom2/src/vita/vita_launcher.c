#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <vitaGL.h>

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

#define LAUNCHER_FONT_COLS 16
#define LAUNCHER_FONT_ROWS 16
#define LAUNCHER_FONT_W 8
#define LAUNCHER_FONT_H 18
#define LAUNCHER_ATLAS_W (LAUNCHER_FONT_COLS * LAUNCHER_FONT_W)
#define LAUNCHER_ATLAS_H (LAUNCHER_FONT_ROWS * LAUNCHER_FONT_H)

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

static GLuint launcher_font_texture;

static void Vita_LauncherSet2D(void)
{
  glViewport(0, 0, VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, VITA_DISPLAY_WIDTH, VITA_DISPLAY_HEIGHT, 0.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
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
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, a);

  glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
  glEnd();
}

static int Vita_LauncherCreateFont(void)
{
  unsigned char *pixels;
  int ch;
  int cy;
  int cx;

  if (launcher_font_texture)
    return 1;

  pixels = calloc((size_t)LAUNCHER_ATLAS_W * LAUNCHER_ATLAS_H, 4);
  if (!pixels)
    return 0;

  for (ch = 0; ch < 256; ++ch)
  {
    const int glyph_x = (ch % LAUNCHER_FONT_COLS) * LAUNCHER_FONT_W;
    const int glyph_y = (ch / LAUNCHER_FONT_COLS) * LAUNCHER_FONT_H;

    for (cy = 0; cy < LAUNCHER_FONT_H; ++cy)
    {
      const unsigned char row = normal_font.data[ch * LAUNCHER_FONT_H + cy];

      for (cx = 0; cx < LAUNCHER_FONT_W; ++cx)
      {
        const int atlas_x = glyph_x + cx;
        const int atlas_y = glyph_y + cy;
        const size_t offset =
          ((size_t)atlas_y * LAUNCHER_ATLAS_W + atlas_x) * 4;

        pixels[offset + 0] = 255;
        pixels[offset + 1] = 255;
        pixels[offset + 2] = 255;
        pixels[offset + 3] = (row & (1u << cx)) ? 255 : 0;
      }
    }
  }

  glGenTextures(1, &launcher_font_texture);
  glBindTexture(GL_TEXTURE_2D, launcher_font_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    LAUNCHER_ATLAS_W,
    LAUNCHER_ATLAS_H,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    pixels
  );

  free(pixels);
  return launcher_font_texture != 0;
}

static void Vita_LauncherDestroyFont(void)
{
  if (launcher_font_texture)
  {
    glDeleteTextures(1, &launcher_font_texture);
    launcher_font_texture = 0;
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
  const float glyph_w = LAUNCHER_FONT_W * scale;
  const float glyph_h = LAUNCHER_FONT_H * scale;
  float cursor_x = x;

  if (!text || !launcher_font_texture)
    return;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, launcher_font_texture);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, a);

  glBegin(GL_QUADS);

  while (*text)
  {
    const unsigned char ch = (unsigned char)*text++;
    const int col = ch % LAUNCHER_FONT_COLS;
    const int row = ch / LAUNCHER_FONT_COLS;
    const float u0 = (float)(col * LAUNCHER_FONT_W) / LAUNCHER_ATLAS_W;
    const float v0 = (float)(row * LAUNCHER_FONT_H) / LAUNCHER_ATLAS_H;
    const float u1 = (float)((col + 1) * LAUNCHER_FONT_W) / LAUNCHER_ATLAS_W;
    const float v1 = (float)((row + 1) * LAUNCHER_FONT_H) / LAUNCHER_ATLAS_H;

    glTexCoord2f(u0, v0);
    glVertex2f(cursor_x, y);
    glTexCoord2f(u1, v0);
    glVertex2f(cursor_x + glyph_w, y);
    glTexCoord2f(u1, v1);
    glVertex2f(cursor_x + glyph_w, y + glyph_h);
    glTexCoord2f(u0, v1);
    glVertex2f(cursor_x, y + glyph_h);

    cursor_x += glyph_w;
  }

  glEnd();
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

  Vita_LauncherSet2D();

  glClearColor(0.015f, 0.020f, 0.028f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

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

  vglSwapBuffers(GL_FALSE);
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

  if (!Vita_VideoInit())
  {
    Vita_Log("[VITA] launcher: VitaGL presentation init returned failure\n");
    return 0;
  }

  if (!Vita_LauncherCreateFont())
  {
    Vita_Log("[VITA] launcher: unable to create font texture\n");
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

          Vita_LauncherDestroyFont();
          return 1;
        }
      }
    }

    if (pressed & SCE_CTRL_CIRCLE)
    {
      Vita_Log("[VITA] launcher exit requested\n");
      Vita_LauncherDestroyFont();
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
