/*
 * C23-ish <stdarg.h> for %C (decls + macros → modc builtins → QBE).
 * va_arg of structs/unions is not supported by the emitter (scalars/pointers).
 *
 * Layout matches the QBE target: amd64_sysv uses a 32-byte list; amd64_win
 * stores a single pointer (Microsoft x64). Host == target today.
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
