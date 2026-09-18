/*
 * C23 <stdlib.h> for %C — declaration-only; host libc at link.
 * Skipped: Annex K (*_s); long double converters (no long double in %C).
 */
#pragma once

#include <stddef.h>

typedef struct {
	int quot;
	int rem;
} div_t;

typedef struct {
	long quot;
	long rem;
} ldiv_t;

typedef struct {
	long long quot;
	long long rem;
} lldiv_t;

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define RAND_MAX 0x7fffffff
#define MB_CUR_MAX 4

double atof(const char *nptr);
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

double strtod(const char *restrict nptr, char **restrict endptr);
float strtof(const char *restrict nptr, char **restrict endptr);

long strtol(const char *restrict nptr, char **restrict endptr, int base);
long long strtoll(const char *restrict nptr, char **restrict endptr, int base);
unsigned long strtoul(const char *restrict nptr, char **restrict endptr, int base);
unsigned long long strtoull(const char *restrict nptr, char **restrict endptr, int base);

int rand(void);
void srand(unsigned seed);

void *aligned_alloc(size_t alignment, size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);

void abort(void);
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
void exit(int status);
void _Exit(int status);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
void quick_exit(int status);
int system(const char *command);

/* Returns a pointer into base; const? preserves call-site constness. */
const? void *bsearch(const void *key, const? void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *));
void qsort(void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *));

int abs(int j);
long labs(long j);
long long llabs(long long j);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

int mblen(const char *s, size_t n);
int mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *restrict dst, const char *restrict src, size_t len);
size_t wcstombs(char *restrict dst, const wchar_t *restrict src, size_t len);

