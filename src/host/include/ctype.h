/*
 * C <ctype.h> for %C — declaration-only; host libc at link.
 * Avoid host ctype.h when cross-compiling: Darwin's pulls size_t as
 * unsigned long, which is 4 bytes under LLP64 (--target=windows).
 */
#pragma once

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);
