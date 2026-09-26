#include "i_capture.h"
#include "lprintf.h"

#include "vita/vita_system.h"

int capturing_video;
int cap_fps;
int cap_frac;
int cap_wipescreen;

void I_CapturePrep(const char *fn)
{
  (void)fn;
  capturing_video = 0;
  lprintf(LO_WARN, "Video capture is not supported on the Vita build\n");
  Vita_Log("[VITA] -viddump requested: unsupported\n");
}

void I_CaptureFrame(void)
{
}

void I_CaptureFinish(void)
{
  capturing_video = 0;
}
