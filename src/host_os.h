/*
 * Host OS helpers: paths, dirs, processes.
 * POSIX and Win32 implementations live in host_os.c.
 */
#ifndef MODC_HOST_OS_H
#define MODC_HOST_OS_H

#include <stddef.h>

#ifndef HOST_PATH_MAX
#define HOST_PATH_MAX 4096
#endif

typedef struct HostDir HostDir;

int host_path_is_sep(int c);
int host_path_is_abs(const char* path);
const char* host_path_last_sep(const char* path);
void host_dirname(const char* path, char* out, size_t n);
char* host_join_path(const char* a, const char* b); /* heap; always uses '/' */
void host_path_slashify(char* path);		    /* '\\' → '/' in place */

int host_realpath(const char* path, char* out, size_t n); /* 0 ok */
int host_abspath(const char* path, char* out, size_t n);  /* 0 ok; cwd-relative OK */
int host_exe_path(char* out, size_t n, const char* argv0); /* 0 ok */
int host_getcwd(char* out, size_t n);			   /* 0 ok */

int host_is_dir(const char* path);
int host_is_file(const char* path);
int host_exists(const char* path);
int host_access_read(const char* path); /* 0 if readable */

int host_mkdir(const char* path);  /* 0 ok; EEXIST → 0 */
int host_rmdir(const char* path);  /* 0 ok */
int host_unlink(const char* path); /* 0 ok */
int host_rmtree(const char* path); /* 0 ok; recursive */

/* Create unique temp directory; writes path into out. prefix e.g. "modc-build". */
int host_mkdtemp(char* out, size_t n, const char* prefix);

HostDir* host_opendir(const char* path);
const char* host_readdir(HostDir* d); /* basename; NULL at end */
void host_closedir(HostDir* d);

/* Spawn argv[0] with argv (NULL-terminated); returns exit status or -1. */
int host_spawn_wait(const char* const argv[]);
/* Spawn argv and capture the first stdout token; 0 on success. */
int host_spawn_capture(const char* const argv[], char* out, size_t out_len);

#endif
