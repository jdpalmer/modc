/*
 * Prefer #include <stdarg.h> from the hosted include tree.
 * Duplicate kept so old doc links to this path still resolve.
 */
#pragma once

typedef struct __va_list_struct __va_list_struct;
struct __va_list_struct {
#ifdef _WIN32
	char __a[8];
#else
	char __a[32];
#endif
};
typedef __va_list_struct va_list[1];

void __builtin_va_start(void *, long);
void __builtin_va_end(void *);
long __builtin_va_arg(void *, void *);
void __builtin_va_copy(void *, void *);

#define va_start(ap, last)	__builtin_va_start((ap), 0)
#define va_end(ap)		__builtin_va_end((ap))
#define va_arg(ap, type)	__builtin_va_arg((ap), (type *)0)
#define va_copy(dst, src)	__builtin_va_copy((dst), (src))
