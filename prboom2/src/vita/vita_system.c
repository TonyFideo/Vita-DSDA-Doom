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
static unsigned int vita_profile_frames;
static unsigned long long vita_profile_stage_us[VITA_PROFILE_COUNT];

unsigned int vita_profile_plane_cache_hits;
unsigned int vita_profile_plane_cache_misses;

void Vita_ProfileReset(void)
{
  memset(vita_profile_stage_us, 0, sizeof(vita_profile_stage_us));
  vita_profile_frames = 0;
  vita_profile_plane_cache_hits = 0;
  vita_profile_plane_cache_misses = 0;
  vita_profile_active = 1;

  Vita_Log("[VITA][PROFILE] timedemo profiler started\n");
}

int Vita_ProfileActive(void)
{
  return vita_profile_active;
}

unsigned int Vita_ProfileTimestamp(void)
{
  return sceKernelGetProcessTimeLow();
}

void Vita_ProfileAdd(vita_profile_stage_t stage, unsigned int usec)
{
  if (!vita_profile_active || stage < 0 || stage >= VITA_PROFILE_COUNT)
    return;

  vita_profile_stage_us[stage] += usec;
}

void Vita_ProfileFrame(void)
{
  if (vita_profile_active)
    ++vita_profile_frames;
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
