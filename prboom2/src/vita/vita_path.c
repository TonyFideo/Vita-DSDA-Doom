#include <ctype.h>

#include "vita/vita_path.h"

#define VITA_DEVICE_NAME_MAX 15

int Vita_IsDevicePath(const char *name)
{
  int i;

  if (!name)
    return 0;

  for (i = 0; i <= VITA_DEVICE_NAME_MAX && name[i]; ++i)
  {
    const unsigned char c = (unsigned char)name[i];

    if (c == ':')
      return i > 0;

    if (!isalnum(c))
      return 0;
  }

  return 0;
}
