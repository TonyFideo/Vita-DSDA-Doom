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

#endif
