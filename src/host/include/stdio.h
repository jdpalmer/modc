/*
 * C23 <stdio.h> for %C — declaration-only; host libc at link.
 * Skipped: Annex K (*_s); wide-character I/O (see <wchar.h> on the system).
 */
#pragma once

#include <stddef.h>
#include <stdarg.h>

typedef struct FILE FILE;
typedef long long fpos_t;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 1024
#define FILENAME_MAX 1024
#define FOPEN_MAX 20
#define TMP_MAX 308915776
#define L_tmpnam 1024

#ifdef __APPLE__
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin __stdinp
#define stdout __stdoutp
#define stderr __stderrp
#elif defined(_WIN32)
/* PE: QBE amd64_win cannot take the address of dllimport data; use UCRT
 * accessors (same as MinGW/MSVC headers) so stdin/out/err are calls.
 * Related: errno → *_errno(), environ → *__p__environ(); see docs/interop.md
 * for other CRT globals not yet stubbed. */
FILE *__acrt_iob_func(unsigned index);
#define stdin (__acrt_iob_func(0))
#define stdout (__acrt_iob_func(1))
#define stderr (__acrt_iob_func(2))
#else
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#endif

int remove(const char *filename);
int rename(const char *old, const char *new);
[[provides]] FILE *tmpfile(void);
char *tmpnam(char *s);

int fclose([[releases]] FILE *stream);
int fflush(FILE *stream);
[[provides]] FILE *fopen(const char *restrict filename, const char *restrict mode);
[[provides]] FILE *fdopen(int fd, const char *mode);
[[provides]] FILE *popen(const char *command, const char *mode);
int pclose([[releases]] FILE *stream);
[[provides]] FILE *freopen(const char *restrict filename, const char *restrict mode,
			   [[releases]] FILE *restrict stream);
void setbuf(FILE *restrict stream, char *restrict buf);
int setvbuf(FILE *restrict stream, char *restrict buf, int mode, size_t size);

[[format(printf, 2)]] int fprintf(FILE *restrict stream, const char *restrict format, ...);
[[format(scanf, 2)]] int fscanf(FILE *restrict stream, const char *restrict format, ...);
[[format(printf, 1)]] int printf(const char *restrict format, ...);
[[format(scanf, 1)]] int scanf(const char *restrict format, ...);
[[format(printf, 3)]] int snprintf(char *restrict s, size_t n, const char *restrict format,
				   ...);
[[deprecated("use snprintf"), format(printf, 2)]] int sprintf(
    char *restrict s, const char *restrict format, ...);
[[format(scanf, 2)]] int sscanf(const char *restrict s, const char *restrict format, ...);
int vfprintf(FILE *restrict stream, const char *restrict format, va_list arg);
int vfscanf(FILE *restrict stream, const char *restrict format, va_list arg);
int vprintf(const char *restrict format, va_list arg);
int vscanf(const char *restrict format, va_list arg);
int vsnprintf(char *restrict s, size_t n, const char *restrict format, va_list arg);
[[deprecated("use vsnprintf")]] int vsprintf(char *restrict s, const char *restrict format,
					     va_list arg);
int vsscanf(const char *restrict s, const char *restrict format, va_list arg);

int fgetc(FILE *stream);
[[deprecated("use fgets or a bounded reader")]] char *gets(char *s);
char *fgets(char *restrict s, int n, FILE *restrict stream);
int fputc(int c, FILE *stream);
int fputs(const char *restrict s, FILE *restrict stream);
int getc(FILE *stream);
int getchar(void);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
int ungetc(int c, FILE *stream);

size_t fread(void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream);
size_t fwrite(const void *restrict ptr, size_t size, size_t nmemb,
    FILE *restrict stream);

int fgetpos(FILE *restrict stream, fpos_t *restrict pos);
int fseek(FILE *stream, long offset, int whence);
int fsetpos(FILE *stream, const fpos_t *pos);
long ftell(FILE *stream);
void rewind(FILE *stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

