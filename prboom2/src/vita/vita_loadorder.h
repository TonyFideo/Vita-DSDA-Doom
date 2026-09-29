#ifndef DSDA_VITA_LOADORDER_H
#define DSDA_VITA_LOADORDER_H

/*
 * Ordered, bounded selection of launcher files (PWADs or DEH/BEX patches).
 *
 * The list stores indices into the launcher's discovered-file table, in the
 * order the user picked them. The engine loads -file and -deh arguments in
 * command-line order, so this order is the load order. Platform independent
 * so it can be unit tested on the host.
 */

#define VITA_LOADORDER_MAX 16

typedef struct
{
  int count;
  int items[VITA_LOADORDER_MAX];
} vita_loadorder_t;

typedef enum
{
  VITA_FILE_OTHER = 0,
  VITA_FILE_WAD,
  VITA_FILE_DEH
} vita_file_kind_t;

typedef enum
{
  VITA_LOADORDER_REMOVED = 0,
  VITA_LOADORDER_ADDED = 1,
  VITA_LOADORDER_FULL = -1,
  VITA_LOADORDER_INVALID = -2
} vita_loadorder_result_t;

void Vita_LoadOrderClear(vita_loadorder_t *order);

/* Position (0-based) of index in the order, or -1 when absent. */
int Vita_LoadOrderPosition(const vita_loadorder_t *order, int index);

/*
 * Adds index at the end when absent, removes it (keeping the relative order of
 * the rest) when present. index must be in [0, available).
 */
vita_loadorder_result_t Vita_LoadOrderToggle(
  vita_loadorder_t *order,
  int index,
  int available);

/* Last selected index, or -1 when the order is empty. */
int Vita_LoadOrderLast(const vita_loadorder_t *order);

/* Classifies a file name by extension: .wad, .deh or .bex (case-insensitive). */
vita_file_kind_t Vita_ClassifyFileName(const char *name);

typedef const char *(*vita_loadorder_path_fn)(int index);
typedef void (*vita_loadorder_emit_fn)(int is_deh, const char *path);

/*
 * Emits the load order: every PWAD in order, then every DEH/BEX in order,
 * matching "IWAD, followed by PWADs and finally DEHs". Indices whose path
 * lookup returns NULL are skipped. Returns the number of paths emitted.
 */
int Vita_LoadOrderEmit(
  const vita_loadorder_t *pwads,
  vita_loadorder_path_fn pwad_path,
  const vita_loadorder_t *dehs,
  vita_loadorder_path_fn deh_path,
  vita_loadorder_emit_fn emit);

#endif
