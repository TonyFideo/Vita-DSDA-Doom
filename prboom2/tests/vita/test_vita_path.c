/* Host unit tests for prboom2/src/vita/vita_path.c */
#include <stdio.h>

#include "vita/vita_path.h"

static int failures;
static int checks;

#define EXPECT(name, want) do { ++checks; if ((Vita_IsDevicePath(name) != 0) != (want)) { ++failures; \
  fprintf(stderr, "FAIL %s:%d: Vita_IsDevicePath(%s%s%s) != %d\n", __FILE__, __LINE__, \
          (name) ? "\"" : "", (name) ? (name) : "NULL", (name) ? "\"" : "", (want)); } } while (0)

int main(void)
{
  /* device-qualified absolute paths */
  EXPECT("ux0:/data/DSDA-Doom/PWADs/beta second.wad", 1);
  EXPECT("ux0:data/DSDA-Doom/PWADs/a.wad", 1);
  EXPECT("uma0:/data/DSDA-Doom/PWADs/c.WAD", 1);
  EXPECT("ur0:/data/x.deh", 1);
  EXPECT("app0:/dsda-doom.wad", 1);
  EXPECT("app0:", 1);
  EXPECT("UX0:/DATA/X.WAD", 1);
  /* not device paths */
  EXPECT("/data/DSDA-Doom/x.wad", 0);
  EXPECT("x.wad", 0);
  EXPECT("my map pack.wad", 0);
  EXPECT("dir/a:b.wad", 0);
  EXPECT("dir name/ux0:x.wad", 0);
  EXPECT(":x.wad", 0);
  EXPECT("ux 0:/x.wad", 0);
  EXPECT("ux-0:/x.wad", 0);
  EXPECT("averyveryverylongdevice0:/x", 0);
  EXPECT("", 0);
  EXPECT(NULL, 0);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
