#include <string.h>
#include <strings.h>

#include "vita/vita_loadorder.h"

void Vita_LoadOrderClear(vita_loadorder_t *order)
{
  if (!order)
    return;

  order->count = 0;
}

int Vita_LoadOrderPosition(const vita_loadorder_t *order, int index)
{
  int i;

  if (!order)
    return -1;

  for (i = 0; i < order->count && i < VITA_LOADORDER_MAX; ++i)
  {
    if (order->items[i] == index)
      return i;
  }

  return -1;
}

static void Vita_LoadOrderRemoveAt(vita_loadorder_t *order, int position)
{
  int i;

  for (i = position; i + 1 < order->count && i + 1 < VITA_LOADORDER_MAX; ++i)
    order->items[i] = order->items[i + 1];

  --order->count;
}

vita_loadorder_result_t Vita_LoadOrderToggle(
  vita_loadorder_t *order,
  int index,
  int available)
{
  int position;

  if (!order || index < 0 || index >= available)
    return VITA_LOADORDER_INVALID;

  if (order->count < 0 || order->count > VITA_LOADORDER_MAX)
    return VITA_LOADORDER_INVALID;

  position = Vita_LoadOrderPosition(order, index);
  if (position >= 0)
  {
    Vita_LoadOrderRemoveAt(order, position);
    return VITA_LOADORDER_REMOVED;
  }

  if (order->count >= VITA_LOADORDER_MAX)
    return VITA_LOADORDER_FULL;

  order->items[order->count] = index;
  ++order->count;
  return VITA_LOADORDER_ADDED;
}

int Vita_LoadOrderLast(const vita_loadorder_t *order)
{
  if (!order || order->count <= 0 || order->count > VITA_LOADORDER_MAX)
    return -1;

  return order->items[order->count - 1];
}

static int Vita_HasSuffix(const char *name, size_t length, const char *suffix)
{
  const size_t suffix_length = strlen(suffix);

  if (length <= suffix_length)
    return 0;

  return strcasecmp(name + length - suffix_length, suffix) == 0;
}

vita_file_kind_t Vita_ClassifyFileName(const char *name)
{
  size_t length;

  if (!name)
    return VITA_FILE_OTHER;

  length = strlen(name);

  if (Vita_HasSuffix(name, length, ".wad"))
    return VITA_FILE_WAD;

  if (Vita_HasSuffix(name, length, ".deh") || Vita_HasSuffix(name, length, ".bex"))
    return VITA_FILE_DEH;

  return VITA_FILE_OTHER;
}

static int Vita_LoadOrderEmitList(
  const vita_loadorder_t *order,
  vita_loadorder_path_fn path_at,
  int is_deh,
  vita_loadorder_emit_fn emit)
{
  int emitted = 0;
  int i;

  if (!order || !path_at)
    return 0;

  for (i = 0; i < order->count && i < VITA_LOADORDER_MAX; ++i)
  {
    const char *path = path_at(order->items[i]);

    if (!path)
      continue;

    emit(is_deh, path);
    ++emitted;
  }

  return emitted;
}

int Vita_LoadOrderEmit(
  const vita_loadorder_t *pwads,
  vita_loadorder_path_fn pwad_path,
  const vita_loadorder_t *dehs,
  vita_loadorder_path_fn deh_path,
  vita_loadorder_emit_fn emit)
{
  int emitted;

  if (!emit)
    return 0;

  emitted = Vita_LoadOrderEmitList(pwads, pwad_path, 0, emit);
  emitted += Vita_LoadOrderEmitList(dehs, deh_path, 1, emit);
  return emitted;
}
