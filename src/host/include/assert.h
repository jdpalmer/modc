#pragma once

/*
 * assert.h — C23 §7.2 (modc stub).
 *
 * Uses __FILE__/__LINE__ (preprocessor predefined).
 * Comma operator is banned in %C; failure goes through a helper.
 * Uses write(2) rather than fprintf(stderr, …): Apple's __stderrp is a
 * dylib symbol that QBE currently addresses incorrectly (needs GOT).
 * Does not redefine static_assert (language keyword).
 */

#include <stdio.h>
#include <stdlib.h>

long write(int fd, const void *buf, unsigned long n);

#ifndef NDEBUG
static int
__modc_assert_fail(const char *file, int line, const char *expr)
{
	char buf[512];
	int n;

	n = snprintf(buf, sizeof(buf), "%s:%d: assert failed: %s\n",
	    file, line, expr);
	if(n > 0) {
		if(n >= (int)sizeof(buf))
			n = (int)sizeof(buf) - 1;
		write(2, buf, (unsigned long)n);
	}
	abort();
	return 0;
}
#endif

#ifdef NDEBUG
#define assert(expr) ((void)0)
#define assert_eq(a, b) ((void)0)
#else
#define assert(expr) \
	((void)((expr) || __modc_assert_fail(__FILE__, __LINE__, #expr)))
#define assert_eq(a, b) \
	((void)(((a) == (b)) || __modc_assert_fail(__FILE__, __LINE__, #a " == " #b)))
#endif
