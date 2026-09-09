/*
 * C23 <stddef.h> for %C user programs (not the compiler build).
 * size_t / ptrdiff_t / uintptr-width types use long long for LLP64 Windows.
 * Skipped: nullptr_t / nullptr (%C rejects nullptr); unreachable (no builtin yet).
 */
#pragma once

typedef unsigned long long size_t;
typedef long long ptrdiff_t;
typedef long long max_align_t;
typedef int wchar_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))
