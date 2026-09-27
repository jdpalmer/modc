/*
 * POSIX <strings.h> for %C — declaration-only; host libc at link.
 * On Windows/MinGW these are provided (or as _stricmp aliases).
 */
#pragma once

#include <stddef.h>

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
