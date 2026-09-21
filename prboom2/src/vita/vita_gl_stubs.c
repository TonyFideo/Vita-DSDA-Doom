#include <stddef.h>

#include "doomdef.h"
#include "doomtype.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_plane.h"
#include "r_things.h"
#include "v_video.h"
#include "gl_struct.h"
#include "dsda/gl/render_scale.h"

/*
 * Link-time compatibility layer for builds which intentionally omit DSDA's
 * OpenGL renderer. Runtime code remains in VID_MODESW, so these functions are
 * never part of the active rendering path. Keeping the API available lets the
 * common engine stay close to upstream while the Vita port is brought up.
 */

int gl_drawskys = skytype_standard;
dboolean gl_ui_lightmode_indexed = false;
dboolean gl_automap_lightmode_indexed = false;
dboolean gl_menu_lightmode_indexed = false;
dboolean gl_use_stencil = false;

const int gl_colorbuffer_bits = 32;
const int gl_depthbuffer_bits = 24;

am_icon_t am_icons[am_icon_count];

int gl_statusbar_height;
int gl_scene_width;
int gl_scene_height;
float gl_scale_x = 1.0f;
float gl_scale_y = 1.0f;
int gl_letterbox_clear_required;

void gld_FlushTextures(void) {}
void gld_InitVertexData(void) {}
void gld_CleanVertexData(void) {}
void gld_UpdateSplitData(sector_t *sector) { (void)sector; }

void gld_Init(int width, int height) { (void)width; (void)height; }
void gld_InitCommandLine(void) {}

void gld_BeginUIDraw(void) {}
void gld_EndUIDraw(void) {}
void gld_BeginAutomapDraw(void) {}
void gld_EndAutomapDraw(void) {}
void gld_BeginMenuDraw(void) {}
void gld_EndMenuDraw(void) {}

void gld_DrawNumPatch(int x, int y, int lump, dboolean center, int cm, enum patch_translation_e flags)
{
  (void)x; (void)y; (void)lump; (void)center; (void)cm; (void)flags;
}

void gld_DrawNumPatch_f(float x, float y, int lump, dboolean center, int cm, enum patch_translation_e flags)
{
  (void)x; (void)y; (void)lump; (void)center; (void)cm; (void)flags;
}

void gld_FillRaw(int lump, int x, int y, int src_width, int src_height,
                 int dst_width, int dst_height, enum patch_translation_e flags)
{
  (void)lump; (void)x; (void)y; (void)src_width; (void)src_height;
  (void)dst_width; (void)dst_height; (void)flags;
}

void gld_FillPatch(int lump, int x, int y, int width, int height, enum patch_translation_e flags)
{
  (void)lump; (void)x; (void)y; (void)width; (void)height; (void)flags;
}

void gld_DrawLine(int x0, int y0, int x1, int y1, int BaseColor)
{
  (void)x0; (void)y0; (void)x1; (void)y1; (void)BaseColor;
}

void gld_DrawLine_f(float x0, float y0, float x1, float y1, int BaseColor)
{
  (void)x0; (void)y0; (void)x1; (void)y1; (void)BaseColor;
}

void gld_DrawWeapon(int weaponlump, vissprite_t *vis, int lightlevel)
{
  (void)weaponlump; (void)vis; (void)lightlevel;
}

void gld_FillBlock(int x, int y, int width, int height, int col)
{
  (void)x; (void)y; (void)width; (void)height; (void)col;
}

void gld_DrawShaded(int x, int y, int width, int height, int shade)
{
  (void)x; (void)y; (void)width; (void)height; (void)shade;
}

void gld_SetPalette(int palette) { (void)palette; }
unsigned char *gld_ReadScreen(void) { return NULL; }

void gld_CleanMemory(void) {}
void gld_CleanStaticMemory(void) {}
void gld_PreprocessLevel(void) {}

void gld_Set2DMode(void) {}
void gld_InitDrawScene(void) {}
void gld_StartDrawScene(void) {}

void gld_AddPlane(int subsectornum, visplane_t *floor, visplane_t *ceiling)
{
  (void)subsectornum; (void)floor; (void)ceiling;
}

void gld_AddWall(seg_t *seg) { (void)seg; }
void gld_ProjectSprite(mobj_t *thing, int lightlevel) { (void)thing; (void)lightlevel; }
void gld_DrawScene(player_t *player) { (void)player; }
void gld_EndDrawScene(void) {}
void gld_Finish(void) {}

int gld_wipe_doMelt(int ticks, int *y_lookup) { (void)ticks; (void)y_lookup; return 0; }
int gld_wipe_exitMelt(int ticks) { (void)ticks; return 0; }
int gld_wipe_StartScreen(void) { return 0; }
int gld_wipe_EndScreen(void) { return 0; }

dboolean gld_clipper_SafeCheckRange(angle_t startAngle, angle_t endAngle)
{
  (void)startAngle; (void)endAngle;
  return true;
}

void gld_clipper_SafeAddClipRange(angle_t startangle, angle_t endangle)
{
  (void)startangle; (void)endangle;
}

void gld_FrustumSetup(void) {}

dboolean gld_SphereInFrustum(float x, float y, float z, float radius)
{
  (void)x; (void)y; (void)z; (void)radius;
  return true;
}

sector_t *GetBestFake(sector_t *sector, int ceiling, int validcount)
{
  (void)ceiling; (void)validcount;
  return sector;
}

sector_t *GetBestBleedSector(sector_t *source, enum bleedtype type)
{
  (void)type;
  return source;
}

void gld_DrawMapLines(void) {}
void gld_MultisamplingInit(void) {}
void gld_MultisamplingSet(void) {}
void gld_ProcessTexturedMap(void) {}
void gld_ResetTexturedAutomap(void) {}

void gld_MapDrawSubsectors(player_t *plr, int fx, int fy, fixed_t mx, fixed_t my,
                           int fw, int fh, fixed_t scale)
{
  (void)plr; (void)fx; (void)fy; (void)mx; (void)my; (void)fw; (void)fh; (void)scale;
}

void gld_Init8InGLMode(void) {}
void gld_Draw8InGL(void) {}
void gld_InitMapPics(void) {}

void gld_AddNiceThing(int type, float x, float y, float radius, float angle,
                      unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  (void)type; (void)x; (void)y; (void)radius; (void)angle;
  (void)r; (void)g; (void)b; (void)a;
}

void gld_DrawNiceThings(int fx, int fy, int fw, int fh)
{
  (void)fx; (void)fy; (void)fw; (void)fh;
}

void gld_ClearNiceThings(void) {}

void gld_ResetAutomapTransparency(void) {}
void gld_ResetShadowParameters(void) {}

void dsda_GLSetRenderViewportParams(void) {}
void dsda_GLSetRenderViewport(void) {}
void dsda_GLSetRenderViewportScissor(void) {}
void dsda_GLSetRenderSceneScissor(void) {}

void dsda_GLSetScreenSpaceScissor(int x, int y, int w, int h)
{
  (void)x; (void)y; (void)w; (void)h;
}

void dsda_GLUpdateStatusBarVisible(void) {}
void dsda_GLLetterboxClear(void) {}
void dsda_GLStartMeltRenderTexture(void) {}
void dsda_GLEndMeltRenderTexture(void) {}
void dsda_GLFullscreenOrtho2D(void) {}
