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

#include "z_zone.h"

#include "vita/vita_system.h"

#define VITA_PATH_MAX 1024

SceUInt32 sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;

/*
 * Current VitaSDK/binutils releases can place the RX and RW PT_LOAD segments
 * too close together for vita-elf-create to append the generated SCE module
 * metadata (vitasdk/buildscripts#144). Keep a tiny, strongly-aligned object in
 * rodata so the RX segment finishes just after a large alignment boundary and
 * leaves deterministic slack before the RW segment. This consumes address/file
 * padding, not a 128 KiB runtime allocation.
 *
 * Xash3D and other Vita ports use the same workaround while the toolchain
 * regression remains unresolved.
 */
const unsigned char vita_elf_sce_slack
  __attribute__((used, aligned(0x20000))) = 0xff;

static const char *const vita_partitions[] = {
  "ux0:",
  "uma0:",
  "ur0:",
};

static int vita_fs_initialized;
static char vita_data_root[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom";
static char vita_temp_dir[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom/Temp";
static char vita_log_path[VITA_PATH_MAX] = "ux0:/data/DSDA-Doom/Logs/dsda-vita.log";
static char vita_iwad_path[VITA_PATH_MAX];

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

static int Vita_FindFirstWadInDir(const char *dir_path, char *result, size_t result_size)
{
  DIR *dir;
  struct dirent *entry;
  char best_name[VITA_PATH_MAX] = {0};

  dir = opendir(dir_path);
  if (!dir)
    return 0;

  while ((entry = readdir(dir)) != NULL)
  {
    if (!Vita_HasWadExtension(entry->d_name))
      continue;

    if (!best_name[0] || strcasecmp(entry->d_name, best_name) < 0)
      snprintf(best_name, sizeof(best_name), "%s", entry->d_name);
  }

  closedir(dir);

  if (!best_name[0])
    return 0;

  snprintf(result, result_size, "%s/%s", dir_path, best_name);
  return 1;
}

static void Vita_DiscoverIWAD(void)
{
  size_t i;

  vita_iwad_path[0] = '\0';

  for (i = 0; i < sizeof(vita_partitions) / sizeof(vita_partitions[0]); ++i)
  {
    char iwad_dir[VITA_PATH_MAX];

    snprintf(iwad_dir, sizeof(iwad_dir), "%s/data/DSDA-Doom/IWADs", vita_partitions[i]);

    if (Vita_FindFirstWadInDir(iwad_dir, vita_iwad_path, sizeof(vita_iwad_path)))
      return;
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

  Vita_DiscoverIWAD();

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

  vita_fs_initialized = 1;

  Vita_Log("\n=== Vita-DSDA-Doom startup ===\n");
  Vita_Log("[VITA] data root: %s\n", vita_data_root);
  Vita_Log("[VITA] temp dir: %s\n", vita_temp_dir);
  if (vita_iwad_path[0])
    Vita_Log("[VITA] auto IWAD candidate: %s\n", vita_iwad_path);
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

char *Vita_FindAutoIWAD(void)
{
  Vita_InitFilesystem();

  if (!vita_iwad_path[0])
    return NULL;

  return Z_Strdup(vita_iwad_path);
}
