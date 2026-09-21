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

void Vita_ProfileReset(void);
int Vita_ProfileActive(void);
unsigned int Vita_ProfileTimestamp(void);
void Vita_ProfileAdd(vita_profile_stage_t stage, unsigned int usec);
void Vita_ProfileFrame(void);
void Vita_ProfileLog(void);

#endif
