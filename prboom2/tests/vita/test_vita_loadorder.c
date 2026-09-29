/* Host unit tests for prboom2/src/vita/vita_loadorder.c */
#include <stdio.h>
#include <string.h>

#include "vita/vita_loadorder.h"

static int failures;
static int checks;

#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *pwad_table[] = {
  "ux0:/data/DSDA-Doom/PWADs/a.wad",
  "ux0:/data/DSDA-Doom/PWADs/my map pack.wad",
  "uma0:/data/DSDA-Doom/PWADs/c.WAD",
};
static const char *deh_table[] = {
  "ux0:/data/DSDA-Doom/PWADs/fix one.deh",
  "ux0:/data/DSDA-Doom/PWADs/b.BEX",
};

static const char *pwad_at(int i) { return (i >= 0 && i < 3) ? pwad_table[i] : NULL; }
static const char *deh_at(int i) { return (i >= 0 && i < 2) ? deh_table[i] : NULL; }
static const char *null_at(int i) { (void)i; return NULL; }

#define EMIT_MAX 64
static char emitted[EMIT_MAX][128];
static int emitted_deh[EMIT_MAX];
static int emitted_count;

static void record(int is_deh, const char *path)
{
  if (emitted_count >= EMIT_MAX)
    return;
  snprintf(emitted[emitted_count], sizeof(emitted[0]), "%s", path);
  emitted_deh[emitted_count] = is_deh;
  ++emitted_count;
}

static void test_toggle_order_and_removal(void)
{
  vita_loadorder_t o;
  Vita_LoadOrderClear(&o);
  CHECK(o.count == 0);
  CHECK(Vita_LoadOrderLast(&o) == -1);
  CHECK(Vita_LoadOrderToggle(&o, 2, 3) == VITA_LOADORDER_ADDED);
  CHECK(Vita_LoadOrderToggle(&o, 0, 3) == VITA_LOADORDER_ADDED);
  CHECK(Vita_LoadOrderToggle(&o, 1, 3) == VITA_LOADORDER_ADDED);
  CHECK(o.count == 3 && o.items[0] == 2 && o.items[1] == 0 && o.items[2] == 1);
  CHECK(Vita_LoadOrderPosition(&o, 0) == 1);
  CHECK(Vita_LoadOrderLast(&o) == 1);
  /* toggling a selected entry removes it and keeps the rest in order */
  CHECK(Vita_LoadOrderToggle(&o, 0, 3) == VITA_LOADORDER_REMOVED);
  CHECK(o.count == 2 && o.items[0] == 2 && o.items[1] == 1);
  CHECK(Vita_LoadOrderPosition(&o, 0) == -1);
  /* re-adding goes to the end, never duplicates */
  CHECK(Vita_LoadOrderToggle(&o, 0, 3) == VITA_LOADORDER_ADDED);
  CHECK(o.count == 3 && o.items[2] == 0);
  CHECK(Vita_LoadOrderToggle(&o, 2, 3) == VITA_LOADORDER_REMOVED);
  CHECK(Vita_LoadOrderToggle(&o, 1, 3) == VITA_LOADORDER_REMOVED);
  CHECK(Vita_LoadOrderToggle(&o, 0, 3) == VITA_LOADORDER_REMOVED);
  CHECK(o.count == 0 && Vita_LoadOrderLast(&o) == -1);
}

static void test_bounds(void)
{
  vita_loadorder_t o;
  int i;
  Vita_LoadOrderClear(&o);
  CHECK(Vita_LoadOrderToggle(&o, -1, 3) == VITA_LOADORDER_INVALID);
  CHECK(Vita_LoadOrderToggle(&o, 3, 3) == VITA_LOADORDER_INVALID);
  CHECK(Vita_LoadOrderToggle(&o, 0, 0) == VITA_LOADORDER_INVALID);
  CHECK(Vita_LoadOrderToggle(NULL, 0, 3) == VITA_LOADORDER_INVALID);
  CHECK(o.count == 0);
  for (i = 0; i < VITA_LOADORDER_MAX; ++i)
    CHECK(Vita_LoadOrderToggle(&o, i, 100) == VITA_LOADORDER_ADDED);
  CHECK(o.count == VITA_LOADORDER_MAX);
  CHECK(Vita_LoadOrderToggle(&o, VITA_LOADORDER_MAX, 100) == VITA_LOADORDER_FULL);
  CHECK(o.count == VITA_LOADORDER_MAX);
  /* removal still works when full */
  CHECK(Vita_LoadOrderToggle(&o, 0, 100) == VITA_LOADORDER_REMOVED);
  CHECK(o.count == VITA_LOADORDER_MAX - 1 && o.items[0] == 1);
  /* a corrupted count is rejected rather than overrunning items[] */
  o.count = VITA_LOADORDER_MAX + 5;
  CHECK(Vita_LoadOrderToggle(&o, 50, 100) == VITA_LOADORDER_INVALID);
  CHECK(Vita_LoadOrderLast(&o) == -1);
  CHECK(Vita_LoadOrderPosition(&o, 99) == -1);
  Vita_LoadOrderClear(NULL);
}

static void test_classify(void)
{
  CHECK(Vita_ClassifyFileName("doom map.wad") == VITA_FILE_WAD);
  CHECK(Vita_ClassifyFileName("C.WAD") == VITA_FILE_WAD);
  CHECK(Vita_ClassifyFileName("fix.deh") == VITA_FILE_DEH);
  CHECK(Vita_ClassifyFileName("FIX.BeX") == VITA_FILE_DEH);
  CHECK(Vita_ClassifyFileName(".wad") == VITA_FILE_OTHER);
  CHECK(Vita_ClassifyFileName("wad") == VITA_FILE_OTHER);
  CHECK(Vita_ClassifyFileName("notes.txt") == VITA_FILE_OTHER);
  CHECK(Vita_ClassifyFileName("x.wad.bak") == VITA_FILE_OTHER);
  CHECK(Vita_ClassifyFileName("") == VITA_FILE_OTHER);
  CHECK(Vita_ClassifyFileName(NULL) == VITA_FILE_OTHER);
}

static void test_emit_order_with_spaces(void)
{
  vita_loadorder_t p, d;
  Vita_LoadOrderClear(&p);
  Vita_LoadOrderClear(&d);
  Vita_LoadOrderToggle(&p, 1, 3);
  Vita_LoadOrderToggle(&p, 0, 3);
  Vita_LoadOrderToggle(&d, 1, 2);
  Vita_LoadOrderToggle(&d, 0, 2);
  emitted_count = 0;
  CHECK(Vita_LoadOrderEmit(&p, pwad_at, &d, deh_at, record) == 4);
  CHECK(emitted_count == 4);
  CHECK(!strcmp(emitted[0], "ux0:/data/DSDA-Doom/PWADs/my map pack.wad") && !emitted_deh[0]);
  CHECK(!strcmp(emitted[1], "ux0:/data/DSDA-Doom/PWADs/a.wad") && !emitted_deh[1]);
  CHECK(!strcmp(emitted[2], "ux0:/data/DSDA-Doom/PWADs/b.BEX") && emitted_deh[2]);
  CHECK(!strcmp(emitted[3], "ux0:/data/DSDA-Doom/PWADs/fix one.deh") && emitted_deh[3]);
  /* empty lists, NULL lookups and NULL emitter */
  Vita_LoadOrderClear(&p);
  Vita_LoadOrderClear(&d);
  emitted_count = 0;
  CHECK(Vita_LoadOrderEmit(&p, pwad_at, &d, deh_at, record) == 0 && emitted_count == 0);
  Vita_LoadOrderToggle(&p, 0, 3);
  CHECK(Vita_LoadOrderEmit(&p, null_at, &d, deh_at, record) == 0 && emitted_count == 0);
  CHECK(Vita_LoadOrderEmit(&p, pwad_at, NULL, NULL, NULL) == 0);
  CHECK(Vita_LoadOrderEmit(NULL, NULL, NULL, NULL, record) == 0);
}

int main(void)
{
  test_toggle_order_and_removal();
  test_bounds();
  test_classify();
  test_emit_order_with_spaces();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
