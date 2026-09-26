#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <psp2/types.h>
#include <psp2/kernel/processmgr.h>

#include "z_zone.h"

#include "vita/vita_system.h"

#define VITA_PATH_MAX 1024
#define VITA_MAX_WADS 128

SceUInt32 sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;

static const char *const vita_partitions[] = {
  "ux0:",
  "uma0:",
  "ur0:",
};

static int vita_fs_initialized;

static int vita_profile_active;
static int vita_profile_sample_active;
static unsigned int vita_profile_frames;
static unsigned long long vita_profile_stage_us[VITA_PROFILE_COUNT];
static unsigned int vita_profile_last_frame_ts;
static unsigned int vita_profile_interval_frames;
static unsigned long long vita_profile_interval_us;

#define VITA_PROFILE_WINDOW_FRAMES 128u
#define VITA_PROFILE_MAX_WINDOWS 128u

typedef struct
{
  unsigned int frames;
  unsigned int interval_frames;
  unsigned int sample_frames;
  unsigned long long interval_us;
  unsigned long long stage_us[VITA_PROFILE_COUNT];

  unsigned long long wall_us[VITA_WALL_PROFILE_COUNT];
  unsigned int wall_store_calls;
  unsigned int wall_segloop_calls;
  unsigned int wall_column_calls;
  unsigned int wall_flush_calls;
  unsigned long long wall_pixels;

  unsigned long long plane_map_us;
  unsigned long long plane_span_us;
  unsigned int plane_calls;
  unsigned long long plane_pixels;

  unsigned long long masked_sprites;
  unsigned long long masked_actual_candidates;
  unsigned long long masked_old3_candidates;
  unsigned long long masked_build_us;
  unsigned long long masked_clip_us;
  unsigned long long masked_sprite_draw_us;
  unsigned long long masked_rest_us;
  unsigned long long masked_nonpot_columns;
} vita_profile_window_t;

static vita_profile_window_t vita_profile_windows[VITA_PROFILE_MAX_WINDOWS];

static int vita_profile_wall_phase;
static int vita_profile_wall_deep;
static unsigned int vita_profile_wall_sample_frames;
static unsigned long long vita_profile_wall_us[VITA_WALL_PROFILE_COUNT];
static unsigned int vita_profile_wall_store_calls;
static unsigned int vita_profile_wall_segloop_calls;
static unsigned int vita_profile_wall_column_calls;
static unsigned int vita_profile_wall_flush_calls;
static unsigned long long vita_profile_wall_pixels;
static unsigned int vita_profile_wall_fused4_batches;
static unsigned int vita_profile_wall_fused4_fallbacks;
static unsigned long long vita_profile_wall_fused4_common_pixels;
static unsigned int
  vita_profile_wall_fused4_fallback_reason[VITA_WALL_FUSED_FALLBACK_COUNT];
static unsigned int
  vita_profile_wall_fused_reject_reason[VITA_WALL_FUSED_REJECT_COUNT];

static unsigned int vita_profile_plane_sample_calls;
static unsigned long long vita_profile_plane_map_us;
static unsigned long long vita_profile_plane_span_us;
static unsigned long long vita_profile_plane_pixels;

static unsigned int vita_profile_masked_frames;
static unsigned long long vita_profile_masked_sprites;
static unsigned long long vita_profile_masked_actual_candidates;
static unsigned long long vita_profile_masked_old3_candidates;
static unsigned long long vita_profile_masked_build_us;
static unsigned long long vita_profile_masked_clip_us;
static unsigned long long vita_profile_masked_sprite_draw_us;
static unsigned long long vita_profile_masked_rest_us;
static unsigned long long vita_profile_masked_nonpot_columns;

unsigned int vita_profile_plane_cache_hits;
unsigned int vita_profile_plane_cache_misses;

static vita_profile_window_t *Vita_ProfileCurrentWindow(void)
{
  unsigned int index = vita_profile_frames / VITA_PROFILE_WINDOW_FRAMES;

  if (index >= VITA_PROFILE_MAX_WINDOWS)
    index = VITA_PROFILE_MAX_WINDOWS - 1;

  return &vita_profile_windows[index];
}

void Vita_ProfileReset(void)
{
  memset(vita_profile_stage_us, 0, sizeof(vita_profile_stage_us));
  memset(vita_profile_windows, 0, sizeof(vita_profile_windows));
  vita_profile_frames = 0;
  vita_profile_sample_active = 1;
  vita_profile_last_frame_ts = 0;
  vita_profile_interval_frames = 0;
  vita_profile_interval_us = 0;
  vita_profile_plane_cache_hits = 0;
  vita_profile_plane_cache_misses = 0;

  vita_profile_wall_phase = 0;
  vita_profile_wall_deep = 0;
  vita_profile_wall_sample_frames = 0;
  memset(vita_profile_wall_us, 0, sizeof(vita_profile_wall_us));
  vita_profile_wall_store_calls = 0;
  vita_profile_wall_segloop_calls = 0;
  vita_profile_wall_column_calls = 0;
  vita_profile_wall_flush_calls = 0;
  vita_profile_wall_pixels = 0;
  vita_profile_wall_fused4_batches = 0;
  vita_profile_wall_fused4_fallbacks = 0;
  vita_profile_wall_fused4_common_pixels = 0;
  memset(
    vita_profile_wall_fused4_fallback_reason,
    0,
    sizeof(vita_profile_wall_fused4_fallback_reason)
  );
  memset(
    vita_profile_wall_fused_reject_reason,
    0,
    sizeof(vita_profile_wall_fused_reject_reason)
  );

  vita_profile_plane_sample_calls = 0;
  vita_profile_plane_map_us = 0;
  vita_profile_plane_span_us = 0;
  vita_profile_plane_pixels = 0;

  vita_profile_masked_frames = 0;
  vita_profile_masked_sprites = 0;
  vita_profile_masked_actual_candidates = 0;
  vita_profile_masked_old3_candidates = 0;
  vita_profile_masked_build_us = 0;
  vita_profile_masked_clip_us = 0;
  vita_profile_masked_sprite_draw_us = 0;
  vita_profile_masked_rest_us = 0;
  vita_profile_masked_nonpot_columns = 0;

  vita_profile_active = 1;

  Vita_Log("[VITA][PROFILE] timedemo profiler started\n");
}

int Vita_ProfileActive(void)
{
  return vita_profile_active;
}

int Vita_ProfileSampleActive(void)
{
  return vita_profile_active && vita_profile_sample_active;
}

unsigned int Vita_ProfileTimestamp(void)
{
  return sceKernelGetProcessTimeLow();
}

void Vita_ProfileAdd(vita_profile_stage_t stage, unsigned int usec)
{
  if (!vita_profile_active ||
      (unsigned int)stage >= (unsigned int)VITA_PROFILE_COUNT)
    return;

  vita_profile_stage_us[stage] += usec;
  Vita_ProfileCurrentWindow()->stage_us[stage] += usec;
}

void Vita_ProfileFrame(void)
{
  unsigned int now;
  vita_profile_window_t *window;

  if (!vita_profile_active)
    return;

  now = Vita_ProfileTimestamp();
  window = Vita_ProfileCurrentWindow();

  if (vita_profile_last_frame_ts)
  {
    const unsigned int elapsed = now - vita_profile_last_frame_ts;
    vita_profile_interval_us += elapsed;
    ++vita_profile_interval_frames;
    window->interval_us += elapsed;
    ++window->interval_frames;
  }

  if (vita_profile_sample_active)
    ++window->sample_frames;

  ++window->frames;
  vita_profile_last_frame_ts = now;
  ++vita_profile_frames;
  vita_profile_sample_active =
    (vita_profile_frames % VITA_PROFILE_SAMPLE_STRIDE) == 0u;
}

void Vita_ProfileSetWallPhase(int active)
{
  if (!vita_profile_active)
  {
    vita_profile_wall_phase = 0;
    vita_profile_wall_deep = 0;
    return;
  }

  if (active)
  {
    vita_profile_wall_phase = 1;

    /*
     * Deep wall timers are intentionally sampled one frame out of eight.
     * Timing every wall range would perturb the benchmark much more than the
     * coarse per-frame profiler. The timedemo still supplies hundreds of
     * representative samples.
     */
    vita_profile_wall_deep = Vita_ProfileSampleActive();
    if (vita_profile_wall_deep)
      ++vita_profile_wall_sample_frames;
  }
  else
  {
    vita_profile_wall_phase = 0;
    vita_profile_wall_deep = 0;
  }
}

int Vita_ProfileWallDeepActive(void)
{
  return vita_profile_active &&
         vita_profile_wall_phase &&
         vita_profile_wall_deep;
}

void Vita_ProfileWallAdd(vita_wall_profile_stage_t stage, unsigned int usec)
{
  if (!Vita_ProfileWallDeepActive() ||
      (unsigned int)stage >= (unsigned int)VITA_WALL_PROFILE_COUNT)
    return;

  vita_profile_wall_us[stage] += usec;
  Vita_ProfileCurrentWindow()->wall_us[stage] += usec;
}

void Vita_ProfileWallStoreCall(void)
{
  if (Vita_ProfileWallDeepActive())
  {
    ++vita_profile_wall_store_calls;
    ++Vita_ProfileCurrentWindow()->wall_store_calls;
  }
}

void Vita_ProfileWallSegLoopCall(void)
{
  if (Vita_ProfileWallDeepActive())
  {
    ++vita_profile_wall_segloop_calls;
    ++Vita_ProfileCurrentWindow()->wall_segloop_calls;
  }
}

void Vita_ProfileWallColumn(unsigned int pixels)
{
  if (!Vita_ProfileWallDeepActive())
    return;

  ++vita_profile_wall_column_calls;
  vita_profile_wall_pixels += pixels;
  ++Vita_ProfileCurrentWindow()->wall_column_calls;
  Vita_ProfileCurrentWindow()->wall_pixels += pixels;
}

void Vita_ProfileWallFlush(void)
{
  if (Vita_ProfileWallDeepActive())
  {
    ++vita_profile_wall_flush_calls;
    ++Vita_ProfileCurrentWindow()->wall_flush_calls;
  }
}

void Vita_ProfileWallFused4Success(unsigned int common_pixels)
{
  if (!Vita_ProfileWallDeepActive())
    return;

  ++vita_profile_wall_fused4_batches;
  vita_profile_wall_fused4_common_pixels += common_pixels;
}

void Vita_ProfileWallFused4Fallback(vita_wall_fused_fallback_t reason)
{
  if (!Vita_ProfileWallDeepActive())
    return;

  ++vita_profile_wall_fused4_fallbacks;
  if ((unsigned int)reason < VITA_WALL_FUSED_FALLBACK_COUNT)
    ++vita_profile_wall_fused4_fallback_reason[reason];
}

void Vita_ProfileWallFusedReject(vita_wall_fused_reject_t reason)
{
  if (!Vita_ProfileWallDeepActive())
    return;

  if ((unsigned int)reason < VITA_WALL_FUSED_REJECT_COUNT)
    ++vita_profile_wall_fused_reject_reason[reason];
}

void Vita_ProfilePlaneMap(
  unsigned int map_us,
  unsigned int span_us,
  unsigned int pixels
)
{
  vita_profile_window_t *window;

  if (!Vita_ProfileSampleActive())
    return;

  ++vita_profile_plane_sample_calls;
  vita_profile_plane_map_us += map_us;
  vita_profile_plane_span_us += span_us;
  vita_profile_plane_pixels += pixels;

  window = Vita_ProfileCurrentWindow();
  ++window->plane_calls;
  window->plane_map_us += map_us;
  window->plane_span_us += span_us;
  window->plane_pixels += pixels;
}

void Vita_ProfileMaskedFrame(
  unsigned int sprites,
  unsigned int actual_candidates,
  unsigned int old3_candidates,
  unsigned int build_us,
  unsigned int clip_us,
  unsigned int sprite_draw_us,
  unsigned int rest_us
)
{
  vita_profile_window_t *window;

  if (!vita_profile_active)
    return;

  ++vita_profile_masked_frames;
  vita_profile_masked_sprites += sprites;
  vita_profile_masked_actual_candidates += actual_candidates;
  vita_profile_masked_old3_candidates += old3_candidates;
  vita_profile_masked_build_us += build_us;
  vita_profile_masked_clip_us += clip_us;
  vita_profile_masked_sprite_draw_us += sprite_draw_us;
  vita_profile_masked_rest_us += rest_us;

  window = Vita_ProfileCurrentWindow();
  window->masked_sprites += sprites;
  window->masked_actual_candidates += actual_candidates;
  window->masked_old3_candidates += old3_candidates;
  window->masked_build_us += build_us;
  window->masked_clip_us += clip_us;
  window->masked_sprite_draw_us += sprite_draw_us;
  window->masked_rest_us += rest_us;
}

void Vita_ProfileMaskedNonPotColumns(unsigned int columns)
{
  if (!Vita_ProfileSampleActive() || !columns)
    return;

  vita_profile_masked_nonpot_columns += columns;
  Vita_ProfileCurrentWindow()->masked_nonpot_columns += columns;
}

void Vita_ProfileLog(void)
{
  static const char *const names[VITA_PROFILE_COUNT] = {
    "setup",
    "clear",
    "init_scene",
    "bsp_walls",
    "planes",
    "reset_columns",
    "masked",
    "present"
  };
  unsigned long long total = 0;
  unsigned int cache_total;
  unsigned int sample_frames;
  unsigned int window_count;
  int i;

  if (!vita_profile_active)
    return;

  for (i = 0; i < VITA_PROFILE_COUNT; ++i)
    total += vita_profile_stage_us[i];

  Vita_Log(
    "[VITA][PROFILE] frames=%u measured_us=%llu avg_measured_us=%.2f\n",
    vita_profile_frames,
    total,
    vita_profile_frames ? (double)total / vita_profile_frames : 0.0
  );

  if (vita_profile_interval_frames)
  {
    const double frame_avg =
      (double)vita_profile_interval_us / vita_profile_interval_frames;
    const double measured_avg = vita_profile_frames
      ? (double)total / vita_profile_frames : 0.0;

    Vita_Log(
      "[VITA][FRAMEPROFILE] intervals=%u total_us=%llu avg_total_us=%.2f avg_measured_us=%.2f avg_unmeasured_us=%.2f\n",
      vita_profile_interval_frames,
      vita_profile_interval_us,
      frame_avg,
      measured_avg,
      frame_avg > measured_avg ? frame_avg - measured_avg : 0.0
    );
  }

  for (i = 0; i < VITA_PROFILE_COUNT; ++i)
  {
    const double pct = total
      ? (100.0 * (double)vita_profile_stage_us[i] / (double)total)
      : 0.0;
    const double avg = vita_profile_frames
      ? (double)vita_profile_stage_us[i] / vita_profile_frames
      : 0.0;

    Vita_Log(
      "[VITA][PROFILE] %-13s total_us=%llu avg_us=%.2f pct=%.2f\n",
      names[i],
      vita_profile_stage_us[i],
      avg,
      pct
    );
  }

  cache_total =
    vita_profile_plane_cache_hits + vita_profile_plane_cache_misses;

  Vita_Log(
    "[VITA][PROFILE] mapplane_cache hits=%u misses=%u total=%u hit_rate=%.2f%% divisions_avoided=%u\n",
    vita_profile_plane_cache_hits,
    vita_profile_plane_cache_misses,
    cache_total,
    cache_total
      ? 100.0 * (double)vita_profile_plane_cache_hits / cache_total
      : 0.0,
    vita_profile_plane_cache_hits * 2
  );

  sample_frames =
    (vita_profile_frames + VITA_PROFILE_SAMPLE_STRIDE - 1u) /
    VITA_PROFILE_SAMPLE_STRIDE;

  if (sample_frames && vita_profile_plane_sample_calls)
  {
    Vita_Log(
      "[VITA][PLANEPROFILE] sample_frames=%u stride=%u calls=%u map_avg_us=%.2f setup_avg_us=%.2f span_avg_us=%.2f pixels_avg=%.2f pixels_per_call=%.2f\n",
      sample_frames,
      VITA_PROFILE_SAMPLE_STRIDE,
      vita_profile_plane_sample_calls,
      (double)vita_profile_plane_map_us / sample_frames,
      (double)(vita_profile_plane_map_us - vita_profile_plane_span_us) /
        sample_frames,
      (double)vita_profile_plane_span_us / sample_frames,
      (double)vita_profile_plane_pixels / sample_frames,
      (double)vita_profile_plane_pixels / vita_profile_plane_sample_calls
    );
  }

  if (vita_profile_masked_frames)
  {
    Vita_Log(
      "[VITA][MASKEDPROFILE] frames=%u sprites=%llu actual_candidates=%llu old3_candidates=%llu reduction=%.2f%% actual_per_sprite=%.2f old3_per_sprite=%.2f build_avg_us=%.2f\n",
      vita_profile_masked_frames,
      vita_profile_masked_sprites,
      vita_profile_masked_actual_candidates,
      vita_profile_masked_old3_candidates,
      vita_profile_masked_old3_candidates
        ? 100.0 * (
            (double)vita_profile_masked_old3_candidates -
            (double)vita_profile_masked_actual_candidates
          ) / (double)vita_profile_masked_old3_candidates
        : 0.0,
      vita_profile_masked_sprites
        ? (double)vita_profile_masked_actual_candidates /
            vita_profile_masked_sprites
        : 0.0,
      vita_profile_masked_sprites
        ? (double)vita_profile_masked_old3_candidates /
            vita_profile_masked_sprites
        : 0.0,
      (double)vita_profile_masked_build_us / vita_profile_masked_frames
    );

    if (sample_frames)
      Vita_Log(
        "[VITA][MASKEDPROFILE] sample_frames=%u clip_avg_us=%.2f sprite_draw_avg_us=%.2f rest_avg_us=%.2f nonpot_columns=%llu nonpot_avg=%.2f\n",
        sample_frames,
        (double)vita_profile_masked_clip_us / sample_frames,
        (double)vita_profile_masked_sprite_draw_us / sample_frames,
        (double)vita_profile_masked_rest_us / sample_frames,
        vita_profile_masked_nonpot_columns,
        (double)vita_profile_masked_nonpot_columns / sample_frames
      );
  }

  if (vita_profile_wall_sample_frames)
  {
    const double wall_bsp =
      (double)vita_profile_wall_us[VITA_WALL_PROFILE_BSP_SAMPLE] /
      vita_profile_wall_sample_frames;
    const double wall_store =
      (double)vita_profile_wall_us[VITA_WALL_PROFILE_STORE_RANGE] /
      vita_profile_wall_sample_frames;
    const double wall_seg =
      (double)vita_profile_wall_us[VITA_WALL_PROFILE_SEG_LOOP] /
      vita_profile_wall_sample_frames;
    const double wall_other = wall_bsp > wall_store
      ? wall_bsp - wall_store : 0.0;
    const double store_setup = wall_store > wall_seg
      ? wall_store - wall_seg : 0.0;

    Vita_Log(
      "[VITA][WALLPROFILE] sample_frames=%u stride=%u bsp_avg_us=%.2f store_avg_us=%.2f segloop_avg_us=%.2f\n",
      vita_profile_wall_sample_frames,
      VITA_PROFILE_SAMPLE_STRIDE,
      wall_bsp,
      wall_store,
      wall_seg
    );
    Vita_Log(
      "[VITA][WALLPROFILE] bsp_other_avg_us=%.2f store_setup_avg_us=%.2f segloop_pct_bsp=%.2f%%\n",
      wall_other,
      store_setup,
      wall_bsp ? 100.0 * wall_seg / wall_bsp : 0.0
    );
    Vita_Log(
      "[VITA][WALLPROFILE] store_calls=%u segloop_calls=%u columns=%u pixels=%llu flushes=%u\n",
      vita_profile_wall_store_calls,
      vita_profile_wall_segloop_calls,
      vita_profile_wall_column_calls,
      vita_profile_wall_pixels,
      vita_profile_wall_flush_calls
    );
    Vita_Log(
      "[VITA][WALLPROFILE] per_sample_frame stores=%.2f columns=%.2f pixels=%.2f flushes=%.2f pixels_per_column=%.2f\n",
      (double)vita_profile_wall_store_calls / vita_profile_wall_sample_frames,
      (double)vita_profile_wall_column_calls / vita_profile_wall_sample_frames,
      (double)vita_profile_wall_pixels / vita_profile_wall_sample_frames,
      (double)vita_profile_wall_flush_calls / vita_profile_wall_sample_frames,
      vita_profile_wall_column_calls
        ? (double)vita_profile_wall_pixels / vita_profile_wall_column_calls
        : 0.0
    );
    Vita_Log(
      "[VITA][WALLPROFILE] fused4_batches=%u fused4_fallbacks=%u common_pixels=%llu common_pixels_per_sample=%.2f\n",
      vita_profile_wall_fused4_batches,
      vita_profile_wall_fused4_fallbacks,
      vita_profile_wall_fused4_common_pixels,
      (double)vita_profile_wall_fused4_common_pixels /
        vita_profile_wall_sample_frames
    );
    Vita_Log(
      "[VITA][WALLPROFILE] fallback_reason same_x=%u zero_height=%u nonpot=%u x_gap=%u partial_end=%u no_common=%u other=%u\n",
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_SAME_X
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_ZERO_HEIGHT
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_NON_POT
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_X_GAP
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_PARTIAL_END
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_NO_COMMON
      ],
      vita_profile_wall_fused4_fallback_reason[
        VITA_WALL_FUSED_FALLBACK_OTHER
      ]
    );
    Vita_Log(
      "[VITA][WALLPROFILE] reject_columns zero_height=%u nonpot=%u other=%u\n",
      vita_profile_wall_fused_reject_reason[
        VITA_WALL_FUSED_REJECT_ZERO_HEIGHT
      ],
      vita_profile_wall_fused_reject_reason[
        VITA_WALL_FUSED_REJECT_NON_POT
      ],
      vita_profile_wall_fused_reject_reason[
        VITA_WALL_FUSED_REJECT_OTHER
      ]
    );
  }

  window_count =
    (vita_profile_frames + VITA_PROFILE_WINDOW_FRAMES - 1u) /
    VITA_PROFILE_WINDOW_FRAMES;
  if (window_count > VITA_PROFILE_MAX_WINDOWS)
    window_count = VITA_PROFILE_MAX_WINDOWS;

  for (i = 0; i < (int)window_count; ++i)
  {
    const vita_profile_window_t *window = &vita_profile_windows[i];
    unsigned long long window_measured = 0;
    double frame_avg;
    double measured_avg;
    int stage;

    if (!window->frames)
      continue;

    for (stage = 0; stage < VITA_PROFILE_COUNT; ++stage)
      window_measured += window->stage_us[stage];

    frame_avg = window->interval_frames
      ? (double)window->interval_us / window->interval_frames : 0.0;
    measured_avg = (double)window_measured / window->frames;

    Vita_Log(
      "[VITA][WINDOW] idx=%d first=%u frames=%u total_avg_us=%.2f measured_avg_us=%.2f other_avg_us=%.2f bsp_us=%.2f planes_us=%.2f masked_us=%.2f present_us=%.2f\n",
      i,
      (unsigned int)i * VITA_PROFILE_WINDOW_FRAMES,
      window->frames,
      frame_avg,
      measured_avg,
      frame_avg > measured_avg ? frame_avg - measured_avg : 0.0,
      (double)window->stage_us[VITA_PROFILE_BSP_WALLS] / window->frames,
      (double)window->stage_us[VITA_PROFILE_PLANES] / window->frames,
      (double)window->stage_us[VITA_PROFILE_MASKED] / window->frames,
      (double)window->stage_us[VITA_PROFILE_PRESENT] / window->frames
    );
    Vita_Log(
      "[VITA][WINDOWWORK] idx=%d samples=%u wall_stores=%.2f wall_columns=%.2f wall_pixels=%.2f wall_seg_us=%.2f plane_calls=%.2f plane_pixels=%.2f plane_map_us=%.2f plane_span_us=%.2f sprites_per_frame=%.2f cand7_per_sprite=%.2f cand3_per_sprite=%.2f masked_build_us=%.2f masked_clip_us=%.2f masked_draw_us=%.2f masked_rest_us=%.2f nonpot_cols=%.2f\n",
      i,
      window->sample_frames,
      window->sample_frames
        ? (double)window->wall_store_calls / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->wall_column_calls / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->wall_pixels / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->wall_us[VITA_WALL_PROFILE_SEG_LOOP] /
            window->sample_frames
        : 0.0,
      window->sample_frames
        ? (double)window->plane_calls / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->plane_pixels / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->plane_map_us / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->plane_span_us / window->sample_frames : 0.0,
      (double)window->masked_sprites / window->frames,
      window->masked_sprites
        ? (double)window->masked_actual_candidates / window->masked_sprites
        : 0.0,
      window->masked_sprites
        ? (double)window->masked_old3_candidates / window->masked_sprites
        : 0.0,
      (double)window->masked_build_us / window->frames,
      window->sample_frames
        ? (double)window->masked_clip_us / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->masked_sprite_draw_us / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->masked_rest_us / window->sample_frames : 0.0,
      window->sample_frames
        ? (double)window->masked_nonpot_columns / window->sample_frames : 0.0
    );
  }

  vita_profile_active = 0;
}
static char vita_data_root[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom";
static char vita_temp_dir[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom/Temp";
static char vita_log_path[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom/Logs/dsda-vita.log";

static char vita_iwad_paths[VITA_MAX_WADS][VITA_PATH_MAX];
static int vita_iwad_count;
static int vita_selected_iwad;
static char vita_iwad_path[VITA_PATH_MAX];

static char vita_pwad_paths[VITA_MAX_WADS][VITA_PATH_MAX];
static int vita_pwad_count;
static int vita_selected_pwad = -1;

static int Vita_HasWadExtension(const char *name)
{
  size_t length;

  if (!name)
    return 0;

  length = strlen(name);
  if (length < 4)
    return 0;

  return strcasecmp(name + length - 4, ".wad") == 0;
}

static int Vita_WADPartitionRank(const char *path)
{
  size_t i;

  for (i = 0; i < sizeof(vita_partitions) / sizeof(vita_partitions[0]); ++i)
  {
    const size_t length = strlen(vita_partitions[i]);

    if (!strncmp(path, vita_partitions[i], length))
      return (int)i;
  }

  return 99;
}

static int Vita_CompareWADPaths(const void *a, const void *b)
{
  const char *path_a = (const char *)a;
  const char *path_b = (const char *)b;
  const int rank_a = Vita_WADPartitionRank(path_a);
  const int rank_b = Vita_WADPartitionRank(path_b);

  if (rank_a != rank_b)
    return rank_a - rank_b;

  return strcasecmp(path_a, path_b);
}

static void Vita_MakeDir(const char *path)
{
  struct stat st;

  if (!path || !*path)
    return;

  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
    return;

  mkdir(path, 0777);
}

static int Vita_PrepareWritableRoot(const char *partition)
{
  char data_dir[VITA_PATH_MAX];
  char root[VITA_PATH_MAX];
  char path[VITA_PATH_MAX];
  char probe[VITA_PATH_MAX];
  FILE *fp;

  snprintf(data_dir, sizeof(data_dir), "%s/data", partition);
  snprintf(root, sizeof(root), "%s/data/DSDA-Doom", partition);

  Vita_MakeDir(data_dir);
  Vita_MakeDir(root);

  snprintf(probe, sizeof(probe), "%s/.write-test", root);
  fp = fopen(probe, "wb");
  if (!fp)
    return 0;

  fputs("vita-dsda-doom\n", fp);
  fclose(fp);
  remove(probe);

  snprintf(vita_data_root, sizeof(vita_data_root), "%s", root);

  {
    static const char *const subdirs[] = {
      "IWADs",
      "PWADs",
      "Saves",
      "Demos",
      "Screenshots",
      "Temp",
      "Logs",
    };
    size_t i;

    for (i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); ++i)
    {
      snprintf(path, sizeof(path), "%s/%s", vita_data_root, subdirs[i]);
      Vita_MakeDir(path);
    }
  }

  snprintf(vita_temp_dir, sizeof(vita_temp_dir), "%s/Temp", vita_data_root);
  snprintf(vita_log_path, sizeof(vita_log_path), "%s/Logs/dsda-vita.log", vita_data_root);

  return 1;
}

void Vita_RefreshIWADs(void)
{
  char previous[VITA_PATH_MAX];
  size_t partition_index;
  int i;

  snprintf(previous, sizeof(previous), "%s", vita_iwad_path);

  vita_iwad_count = 0;
  vita_selected_iwad = 0;
  vita_iwad_path[0] = '\0';

  for (partition_index = 0;
       partition_index < sizeof(vita_partitions) / sizeof(vita_partitions[0]);
       ++partition_index)
  {
    char iwad_dir[VITA_PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    snprintf(
      iwad_dir,
      sizeof(iwad_dir),
      "%s/data/DSDA-Doom/IWADs",
      vita_partitions[partition_index]
    );

    dir = opendir(iwad_dir);
    if (!dir)
      continue;

    while ((entry = readdir(dir)) != NULL)
    {
      if (!Vita_HasWadExtension(entry->d_name))
        continue;

      if (vita_iwad_count >= VITA_MAX_WADS)
        break;

      snprintf(
        vita_iwad_paths[vita_iwad_count],
        sizeof(vita_iwad_paths[vita_iwad_count]),
        "%s/%s",
        iwad_dir,
        entry->d_name
      );
      ++vita_iwad_count;
    }

    closedir(dir);
  }

  if (vita_iwad_count > 1)
  {
    qsort(
      vita_iwad_paths,
      (size_t)vita_iwad_count,
      sizeof(vita_iwad_paths[0]),
      Vita_CompareWADPaths
    );
  }

  if (previous[0])
  {
    for (i = 0; i < vita_iwad_count; ++i)
    {
      if (!strcasecmp(previous, vita_iwad_paths[i]))
      {
        vita_selected_iwad = i;
        break;
      }
    }
  }

  if (vita_iwad_count > 0)
  {
    snprintf(
      vita_iwad_path,
      sizeof(vita_iwad_path),
      "%s",
      vita_iwad_paths[vita_selected_iwad]
    );
  }
}

void Vita_RefreshPWADs(void)
{
  char previous[VITA_PATH_MAX] = {0};
  size_t partition_index;
  int i;

  if (vita_selected_pwad >= 0 && vita_selected_pwad < vita_pwad_count)
  {
    snprintf(
      previous,
      sizeof(previous),
      "%s",
      vita_pwad_paths[vita_selected_pwad]
    );
  }

  vita_pwad_count = 0;
  vita_selected_pwad = -1;

  for (partition_index = 0;
       partition_index < sizeof(vita_partitions) / sizeof(vita_partitions[0]);
       ++partition_index)
  {
    char pwad_dir[VITA_PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    snprintf(
      pwad_dir,
      sizeof(pwad_dir),
      "%s/data/DSDA-Doom/PWADs",
      vita_partitions[partition_index]
    );

    dir = opendir(pwad_dir);
    if (!dir)
      continue;

    while ((entry = readdir(dir)) != NULL)
    {
      if (!Vita_HasWadExtension(entry->d_name))
        continue;

      if (vita_pwad_count >= VITA_MAX_WADS)
        break;

      snprintf(
        vita_pwad_paths[vita_pwad_count],
        sizeof(vita_pwad_paths[vita_pwad_count]),
        "%s/%s",
        pwad_dir,
        entry->d_name
      );
      ++vita_pwad_count;
    }

    closedir(dir);
  }

  if (vita_pwad_count > 1)
  {
    qsort(
      vita_pwad_paths,
      (size_t)vita_pwad_count,
      sizeof(vita_pwad_paths[0]),
      Vita_CompareWADPaths
    );
  }

  if (previous[0])
  {
    for (i = 0; i < vita_pwad_count; ++i)
    {
      if (!strcasecmp(previous, vita_pwad_paths[i]))
      {
        vita_selected_pwad = i;
        break;
      }
    }
  }
}

void Vita_Log(const char *fmt, ...)
{
  FILE *fp;
  va_list args;

  if (!fmt)
    return;

  fp = fopen(vita_log_path, "ab");
  if (!fp)
    return;

  va_start(args, fmt);
  vfprintf(fp, fmt, args);
  va_end(args);

  fflush(fp);
  fclose(fp);
}

void Vita_InitFilesystem(void)
{
  const char *preferred_partition = "ux0:";
  size_t i;

  if (vita_fs_initialized)
    return;

  Vita_RefreshIWADs();
  Vita_RefreshPWADs();

  if (vita_iwad_path[0])
  {
    for (i = 0; i < sizeof(vita_partitions) / sizeof(vita_partitions[0]); ++i)
    {
      size_t prefix_length = strlen(vita_partitions[i]);

      if (!strncmp(vita_iwad_path, vita_partitions[i], prefix_length))
      {
        preferred_partition = vita_partitions[i];
        break;
      }
    }
  }

  if (!Vita_PrepareWritableRoot(preferred_partition))
    Vita_PrepareWritableRoot("ux0:");

  /*
   * Start every process run with a fresh diagnostic log. Vita_Log itself
   * remains append-only so individual writes cannot accidentally discard
   * earlier messages from the same run.
   */
  {
    FILE *fp = fopen(vita_log_path, "wb");
    if (fp)
      fclose(fp);
  }

  vita_fs_initialized = 1;

  Vita_Log("=== Vita-DSDA-Doom startup ===\n");
  Vita_Log("[VITA] data root: %s\n", vita_data_root);
  Vita_Log("[VITA] temp dir: %s\n", vita_temp_dir);
  Vita_Log("[VITA] IWADs discovered: %d\n", vita_iwad_count);
  Vita_Log("[VITA] PWADs discovered: %d\n", vita_pwad_count);

  if (vita_iwad_path[0])
    Vita_Log("[VITA] default IWAD: %s\n", vita_iwad_path);
  else
    Vita_Log("[VITA] no IWAD found in ux0/uma0/ur0\n");
}

const char *Vita_DataRoot(void)
{
  Vita_InitFilesystem();
  return vita_data_root;
}

const char *Vita_TempDir(void)
{
  Vita_InitFilesystem();
  return vita_temp_dir;
}

const char *Vita_LogPath(void)
{
  Vita_InitFilesystem();
  return vita_log_path;
}

int Vita_IWADCount(void)
{
  Vita_InitFilesystem();
  return vita_iwad_count;
}

const char *Vita_IWADPathAt(int index)
{
  Vita_InitFilesystem();

  if (index < 0 || index >= vita_iwad_count)
    return NULL;

  return vita_iwad_paths[index];
}

const char *Vita_IWADNameAt(int index)
{
  const char *path = Vita_IWADPathAt(index);
  const char *slash;

  if (!path)
    return NULL;

  slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

int Vita_SelectedIWADIndex(void)
{
  Vita_InitFilesystem();
  return vita_selected_iwad;
}

void Vita_SelectIWAD(int index)
{
  Vita_InitFilesystem();

  if (index < 0 || index >= vita_iwad_count)
    return;

  vita_selected_iwad = index;
  snprintf(vita_iwad_path, sizeof(vita_iwad_path), "%s", vita_iwad_paths[index]);
  Vita_Log("[VITA] launcher IWAD selected: %s\n", vita_iwad_path);
}

int Vita_PWADCount(void)
{
  Vita_InitFilesystem();
  return vita_pwad_count;
}

const char *Vita_PWADPathAt(int index)
{
  Vita_InitFilesystem();

  if (index < 0 || index >= vita_pwad_count)
    return NULL;

  return vita_pwad_paths[index];
}

const char *Vita_PWADNameAt(int index)
{
  const char *path = Vita_PWADPathAt(index);
  const char *slash;

  if (!path)
    return NULL;

  slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

int Vita_SelectedPWADIndex(void)
{
  Vita_InitFilesystem();
  return vita_selected_pwad;
}

void Vita_SelectPWAD(int index)
{
  Vita_InitFilesystem();

  if (index < -1 || index >= vita_pwad_count)
    return;

  vita_selected_pwad = index;

  if (index >= 0)
    Vita_Log("[VITA] launcher PWAD selected: %s\n", vita_pwad_paths[index]);
  else
    Vita_Log("[VITA] launcher PWAD selected: none\n");
}

char *Vita_FindAutoIWAD(void)
{
  Vita_InitFilesystem();

  if (!vita_iwad_path[0])
    return NULL;

  return Z_Strdup(vita_iwad_path);
}
