#pragma once

/*
 * assert.h — C23 §7.2 (modc stub).
 *
 * Uses __FILE__/__LINE__ (preprocessor predefined).
 * Comma operator is banned in %C; failure goes through __modc_assert_fail
 * (crash package) so registered cleanups run before abort.
 */

#ifdef NDEBUG
#define assert(expr) ((void)0)
#define assert_eq(a, b) ((void)0)
#else
void __modc_assert_fail(const char *file, int line, const char *expr);

#define assert(expr) \
	((void)((expr) || (__modc_assert_fail(__FILE__, __LINE__, #expr), 0)))
#define assert_eq(a, b) \
	((void)(((a) == (b)) || (__modc_assert_fail(__FILE__, __LINE__, #a " == " #b), 0)))
#endif
