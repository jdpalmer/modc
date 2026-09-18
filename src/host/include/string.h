/*
 * C23 <string.h> for %C — declaration-only; host libc at link.
 * Skipped: Annex K (*_s). No BSD extensions (strlcpy, …).
 *
 * Search APIs that return a pointer into an input use const? so call-site
 * constness is preserved (single prototype; no C++ overload pair).
 */
#pragma once

#include <stddef.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memccpy(void *restrict dest, const void *restrict src, int c, size_t n);
char *strcpy(char *restrict dest, const char *restrict src);
char *strncpy(char *restrict dest, const char *restrict src, size_t n);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

char *strcat(char *restrict dest, const char *restrict src);
char *strncat(char *restrict dest, const char *restrict src, size_t n);

int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);
int strcoll(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strxfrm(char *restrict dest, const char *restrict src, size_t n);

const? void *memchr(const? void *s, int c, size_t n);
const? char *strchr(const? char *s, int c);
size_t strcspn(const char *s, const char *reject);
const? char *strpbrk(const? char *s, const char *accept);
const? char *strrchr(const? char *s, int c);
size_t strspn(const char *s, const char *accept);
const? char *strstr(const? char *haystack, const char *needle);
char *strtok(char *restrict s, const char *restrict delim);

void *memset(void *s, int c, size_t n);
void *memset_explicit(void *s, int c, size_t n);
char *strerror(int errnum);
size_t strlen(const char *s);
