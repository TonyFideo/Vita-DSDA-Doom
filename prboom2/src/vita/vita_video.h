#ifndef DSDA_VITA_VIDEO_H
#define DSDA_VITA_VIDEO_H

int Vita_VideoInit(void);
void Vita_VideoShutdown(void);

int Vita_VideoResize(int width, int height);
void Vita_VideoPresent(const void *rgba_pixels, int pitch, int width, int height);

int Vita_VideoInternalWidth(void);
int Vita_VideoInternalHeight(void);
void Vita_VideoSetInternalResolution(int width, int height);

#endif
