#ifndef DSDA_VITA_SYSTEM_H
#define DSDA_VITA_SYSTEM_H

void Vita_InitFilesystem(void);

const char *Vita_DataRoot(void);
const char *Vita_TempDir(void);
const char *Vita_LogPath(void);

/* Returned string belongs to the zone allocator and must be Z_Free'd. */
char *Vita_FindAutoIWAD(void);

void Vita_Log(const char *fmt, ...);

#endif
