#ifndef DSDA_VITA_PATH_H
#define DSDA_VITA_PATH_H

/*
 * Returns non-zero when name starts with a Vita device prefix such as
 * "ux0:", "uma0:", "ur0:" or "app0:" (1-15 alphanumerics, then ':' before any
 * '/'). Such names are already absolute and must not be appended to a search
 * directory ("app0:/ux0:/data/x.wad" is not a valid Vita path; Vita3K
 * tolerates it by dropping the first device, firmware behaviour is unverified).
 */
int Vita_IsDevicePath(const char *name);

#endif
