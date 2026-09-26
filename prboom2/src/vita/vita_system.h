#ifndef DSDA_VITA_SYSTEM_H
#define DSDA_VITA_SYSTEM_H

void Vita_InitFilesystem(void);
void Vita_RefreshIWADs(void);
void Vita_RefreshPWADs(void);

const char *Vita_DataRoot(void);
const char *Vita_TempDir(void);
const char *Vita_LogPath(void);

int Vita_IWADCount(void);
const char *Vita_IWADPathAt(int index);
const char *Vita_IWADNameAt(int index);
int Vita_SelectedIWADIndex(void);
void Vita_SelectIWAD(int index);

int Vita_PWADCount(void);
const char *Vita_PWADPathAt(int index);
const char *Vita_PWADNameAt(int index);
int Vita_SelectedPWADIndex(void);
void Vita_SelectPWAD(int index);

/* Returned string belongs to the zone allocator and must be Z_Free'd. */
char *Vita_FindAutoIWAD(void);

void Vita_Log(const char *fmt, ...);

typedef enum
{
  VITA_PROFILE_SETUP = 0,
  VITA_PROFILE_CLEAR,
  VITA_PROFILE_INIT_SCENE,
  VITA_PROFILE_BSP_WALLS,
  VITA_PROFILE_PLANES,
  VITA_PROFILE_RESET_COLUMNS,
  VITA_PROFILE_MASKED,
  VITA_PROFILE_PRESENT,
  VITA_PROFILE_COUNT
} vita_profile_stage_t;

extern unsigned int vita_profile_plane_cache_hits;
extern unsigned int vita_profile_plane_cache_misses;

#define VITA_PROFILE_SAMPLE_STRIDE 8u

void Vita_ProfileReset(void);
int Vita_ProfileActive(void);
int Vita_ProfileSampleActive(void);
unsigned int Vita_ProfileTimestamp(void);
void Vita_ProfileAdd(vita_profile_stage_t stage, unsigned int usec);
void Vita_ProfileFrame(void);
void Vita_ProfileLog(void);

typedef enum
{
  VITA_WALL_PROFILE_BSP_SAMPLE = 0,
  VITA_WALL_PROFILE_STORE_RANGE,
  VITA_WALL_PROFILE_SEG_LOOP,
  VITA_WALL_PROFILE_COUNT
} vita_wall_profile_stage_t;

void Vita_ProfileSetWallPhase(int active);
int Vita_ProfileWallDeepActive(void);
void Vita_ProfileWallAdd(vita_wall_profile_stage_t stage, unsigned int usec);
void Vita_ProfileWallStoreCall(void);
void Vita_ProfileWallSegLoopCall(void);
void Vita_ProfileWallColumn(unsigned int pixels);
void Vita_ProfileWallFlush(void);

typedef enum
{
  VITA_WALL_FUSED_FALLBACK_SAME_X = 0,
  VITA_WALL_FUSED_FALLBACK_ZERO_HEIGHT,
  VITA_WALL_FUSED_FALLBACK_NON_POT,
  VITA_WALL_FUSED_FALLBACK_X_GAP,
  VITA_WALL_FUSED_FALLBACK_PARTIAL_END,
  VITA_WALL_FUSED_FALLBACK_NO_COMMON,
  VITA_WALL_FUSED_FALLBACK_OTHER,
  VITA_WALL_FUSED_FALLBACK_COUNT
} vita_wall_fused_fallback_t;

typedef enum
{
  VITA_WALL_FUSED_REJECT_ZERO_HEIGHT = 0,
  VITA_WALL_FUSED_REJECT_NON_POT,
  VITA_WALL_FUSED_REJECT_OTHER,
  VITA_WALL_FUSED_REJECT_COUNT
} vita_wall_fused_reject_t;

void Vita_ProfileWallFused4Success(unsigned int common_pixels);
void Vita_ProfileWallFused4Fallback(vita_wall_fused_fallback_t reason);
void Vita_ProfileWallFusedReject(vita_wall_fused_reject_t reason);

void Vita_ProfilePlaneMap(
  unsigned int map_us,
  unsigned int span_us,
  unsigned int pixels
);

void Vita_ProfileMaskedFrame(
  unsigned int sprites,
  unsigned int actual_candidates,
  unsigned int old3_candidates,
  unsigned int build_us,
  unsigned int clip_us,
  unsigned int sprite_draw_us,
  unsigned int rest_us
);
void Vita_ProfileMaskedNonPotColumns(unsigned int columns);

#endif
