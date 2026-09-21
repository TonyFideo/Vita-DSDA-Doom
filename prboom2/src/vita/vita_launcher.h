#ifndef DSDA_VITA_LAUNCHER_H
#define DSDA_VITA_LAUNCHER_H

typedef enum
{
  VITA_LAUNCHER_RENDERER_SOFTWARE = 0,
  VITA_LAUNCHER_RENDERER_VITAGL = 1
} vita_launcher_renderer_t;

/*
 * Runs the Vita startup launcher.
 * Returns non-zero when the game should start and zero when the user exits.
 */
int Vita_LauncherRun(void);

vita_launcher_renderer_t Vita_LauncherRenderer(void);

#endif
