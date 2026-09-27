# path, fs, os, and tty

These packages are a small standard library for paths, files, the process, and
terminals. They are neither POSIX nor Win32. Public code sees slash paths, `File`
handles, and `(T, bool)` results. There is no `errno` text in this cut.

Native OS types stay behind a package-private `static void *`. Do not copy a
`File` by value (the same hazard as copying a `FILE*`). Pass `File *`, or call
methods on a local `File` the compiler addresses for you. Zero-init
`File f = {0}` is closed.

Import them like any other shipped package:

```c
import "path";
import "fs";
import "os";
import "tty";
```

## `path`

Logical paths always use `/`. Writers fill a caller buffer (`cap(dst)` is room)
and return `(char[..], bool)` — a view into that buffer plus success. Slice
helpers return views into the input and do not allocate. `T[..]` is opaque: use
`len` / `cap` / `ptr`.

On failure, if `cap(dst) > 0` the first byte is set to NUL and no partial path
is written. `cap(dst) == 0` is left unchanged. A successful write stores the
path bytes and, when one extra byte remains, a trailing NUL. Pass `char[N]` or
`ranged(p, 0, n)` for writable space.

- `bool path_is_abs(const char[..] p)` — true if `p` starts with `/`. Drive letters are not absolute until `path_from_sys`.
- `const? char[..] path_dir(const? char[..] p)` — view; `""` if none; `"/"` if root.
- `const? char[..] path_base(const? char[..] p)` — view; trailing slashes ignored; `"/"` for root.
- `(char[..], bool) path_join(char[..] dst, const char[..] a, const char[..] b)` — write `a/b` into `dst`, collapsing slashes at the boundary. An absolute `b` does not replace `a`; leading slashes on `b` are dropped. If `a` is empty and `b` started with `/`, the result stays absolute.
- `(char[..], bool) path_clean(char[..] dst, const char[..] p)` — collapse `//`, `.`, and `..`. `..` does not climb above root or the start of a relative path.
- `(char[..], bool) path_to_sys(char[..] dst, const char[..] p)` — host form. Identity except on Win32, where `/` becomes `\` and a logical drive `/C:/…` becomes `C:\…`.
- `(char[..], bool) path_from_sys(char[..] dst, const char[..] sys)` — logical form. Identity except on Win32, where `\` becomes `/` and a host drive `C:\…` becomes `/C:/…` (so `path_is_abs` is true).

No `stat`, `open`, or host separators in this package.

## `fs`

```c
struct File {
    static void *native;   /* package-private; NULL = closed */
};

enum {
    OpenRead = 1,
    OpenWrite = 2,
    OpenCreate = 4,
    OpenAppend = 8,
    OpenTrunc = 16
};

enum { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

enum { KindFile = 1, KindDir = 2, KindOther = 3 };

struct FileInfo {
    int kind;           /* KindFile, KindDir, or KindOther */
    int64_t size;       /* bytes; 0 unless KindFile */
    int64_t mtime_ns;   /* wall-clock ns since Unix epoch; 0 if unknown */
};

struct Dir {
    static void *native;   /* package-private; NULL = closed */
};

enum { DefaultBufCap = 4096 };

struct Writer {
    static void *native;
};

struct Reader {
    static void *native;
};

(File, bool) file_open(const char[..] path, int flags);
(File, bool) file_from_fd(int fd);                 /* POSIX; takes ownership */
(File, bool) file_from_fd_borrow(int fd);          /* POSIX; close does not close fd */
(File, bool) file_from_handle(void *h);            /* Win32; takes ownership */
(File, bool) file_from_handle_borrow(void *h);     /* Win32; close does not CloseHandle */
int file_fd(File *f);                              /* -1 if closed / non-POSIX */
void *file_handle(File *f);                        /* NULL if closed / non-Win32 */
(int64_t, bool) (File *f).read(char[..] dst);
(int64_t, bool) (File *f).write(const char[..] src);
(int64_t, bool) (File *f).seek(int64_t off, int whence);
void (File *f).close();

(Writer, bool) writer_open(const char[..] path, int flags, size_t cap);
(Writer, bool) writer_from_file(File *f, size_t cap);
(int64_t, bool) (Writer *w).write(const char[..] src);
bool (Writer *w).flush();
void (Writer *w).close();

(Reader, bool) reader_open(const char[..] path, int flags, size_t cap);
(Reader, bool) reader_from_file(File *f, size_t cap);
(int64_t, bool) (Reader *r).read(char[..] dst);
void (Reader *r).close();

(FileInfo, bool) fs_stat(const char[..] path);
(Dir, bool) dir_open(const char[..] path);
(char[..], bool) (Dir *d).next();
void (Dir *d).close();

bool fs_remove(const char[..] path);          /* file or empty directory */
bool fs_rename(const char[..] from, const char[..] to);
bool fs_mkdir(const char[..] path);           /* one level; no mkdir -p */
```

An open file descriptor of 0 is stored in a heap blob, never as `native`
itself, so a closed handle stays NULL. `close` is idempotent. A null `File *`
is a no-op for `close` and an error for the other methods (an intentional
exception to the usual live-receiver rule in [methods.md](methods.md)).

Result rules:

- `read`: `(n, true)` with `n > 0` is data (short reads are success); `(0, true)` is EOF; `(0, false)` is an error.
- `write`: `(n, true)` bytes written (short writes are allowed; callers loop); `(0, false)` is an error.
- `seek`: `(new_offset, true)` or `(0, false)` (pipes and other non-seekable files fail).
- Flags are an `int` bitmask (`OpenRead | OpenWrite`). `OpenCreate` implies write. `OpenAppend` seeks to end before each write. `OpenTrunc` truncates on open when writing.
- `file_from_fd` / `file_from_handle` take ownership of an existing descriptor (`close` closes it). `file_from_fd_borrow` / `file_from_handle_borrow` wrap without ownership (`close` frees the blob only) — used by `tty` for stdio. There is no public `file_pipe`; `Cmd.pipe` creates pipes internally.

`Writer` / `Reader` buffer on top of `File`. `writer_open` / `reader_open`
own a new `File` (`close` flushes and closes it). `writer_from_file` /
`reader_from_file` borrow an existing `File` (`close` does not close the
`File`; the caller must). `cap == 0` uses `DefaultBufCap` (4096); other
values must be at least 4. Do not mix raw `File` I/O while a `Writer` /
`Reader` is live on that handle. `Poll` only sees the underlying `File`;
bytes still in the buffer are invisible to poll.

`Writer.flush` emits the longest complete UTF-8 prefix and retains a 1–3
byte incomplete trail across flushes. `close` does a flush-all (including any
trail) so data is not lost, then releases the buffer. Large writes flush
pending data then write through with the same trail-hold rule. On a `File`
error, unsent buffer contents are left best-effort unchanged. `Reader`
refills from `File` when empty; `(0, true)` is EOF when the file is at EOF
and the buffer is empty. There is no seek on `Writer` / `Reader` in this cut.

`fs_stat` follows symlinks. A missing path and a dangling symlink are both `!ok`, and the info is zeroed (`kind == 0`). There is no `fs_exists`. `KindOther` is anything that exists and is neither a regular file nor a directory. `size` is meaningful only for `KindFile`. `mtime_ns` is wall-clock time, not `os_mono_ns`.

`dir_open` fails if the path is not a directory. `next` returns a view into the handle, valid until the next `next` or `close`:

- non-empty and `true` — one component (`.` and `..` are skipped)
- empty and `true` — end of the directory
- empty and `false` — error

`Dir` follows the same handle rules as `File`. Do not copy it by value.

Not in this cut: permissions, owner, atime, `lstat`, symlink kind, `fstat`, recursive walk, or `mmap`. Win32 I/O uses `CreateFile` / `FindFirstFile` and follows the same result rules. `path_to_sys` is applied before each call, so callers pass logical slash paths.

## `os`

- `(bool, char[..]) os_env(const char[..] key)` — view into the process environment, valid until the next `os_set_env` of that key (on Win32, until the next `os_env` or `os_set_env`). A missing key is `(false, empty)`.
- `bool os_set_env(const char[..] key, const char[..] val)` — empty `val` unsets. False on failure, or if a NUL-terminated copy cannot be made (internal stack buffer, 4 KiB).
- `(char[..], bool) os_cwd(char[..] dst)` — write the current directory as a logical slash path; return a view into `dst`. False if `dst` is too small or the call fails.
- `bool os_chdir(const char[..] path)` — `path` is logical; Win32 converts with `path_to_sys`.
- `void os_sleep_ms(int64_t ms)` — `ms < 0` is treated as 0. Values above one billion milliseconds are clamped.
- `int64_t os_mono_ns()` — monotonic nanoseconds since an arbitrary epoch; 0 if unavailable.
- `void os_exit(int code)` — does not return.
- `(char[..], bool) os_look_path(char[..] dst, const char[..] name)` — resolve a bare name via `PATH` (or return `name` when it contains `/`). Writes into `dst`.

### `Cmd`

Argv spawn (no shell). Do not copy `Cmd` by value. Zero-init then `init`.

```c
struct Cmd {
    static void *native;
};

enum { StdIn = 0, StdOut = 1, StdErr = 2 };
enum { IoInherit = 0, IoNull = 1 };

void (Cmd *c).init(const char[..] path, const char[..]* args, size_t nargs);
bool (Cmd *c).dir(const char[..] d);
bool (Cmd *c).env(const char[..] key, const char[..] val);
bool (Cmd *c).stdio(int which, int mode);   /* IoInherit | IoNull */
(File, bool) (Cmd *c).pipe(int which);      /* parent end; StdIn write, StdOut/Err read */
bool (Cmd *c).start();
(int, bool) (Cmd *c).wait();            /* block; may already be reaped by Poll */
void (Cmd *c).close();                  /* kill+reap if still running */
```

- `init`: `path` is the program; `args` are the remaining argv. Bare names are resolved with LookPath on `start`.
- Unset stdio defaults to inherit. `IoNull` connects that stream to `/dev/null` (or `NUL`). `pipe` overrides that stream.
- No `try_wait`: use `Poll` to learn that a child has exited, then `wait` for the status (Poll may reap; `wait` still returns the code).

### `Poll`

Wait on `File` and/or `Cmd` without a `File*`/`Cmd*` union slot.

```c
enum { PollIn = 1, PollOut = 2, PollHup = 4, PollErr = 8 };

struct Poll {
    static void *native;
};

void (Poll *p).init();
bool (Poll *p).add_file(File *f, int events);
bool (Poll *p).add_cmd(Cmd *c);
(int, bool) (Poll *p).wait(int64_t timeout_ms); /* n_ready; 0 = timeout */
int (Poll *p).ready_count();
(File*, int) (Poll *p).ready_file(int i);
Cmd* (Poll *p).ready_cmd(int i);
void (Poll *p).reset();
void (Poll *p).close();
```

- Duplicate `add_*` of the same object fails. Cap is 16 registrations in this cut.
- `timeout_ms < 0` blocks; `0` is non-blocking.
- After `wait`, index `0 .. ready_count-1`; each slot is either a file (`ready_file`) or a cmd (`ready_cmd`).

Not in this cut: PTY/foreground, shell-form (`sh -c` / `cmd.exe /C`), `Cmd.wait` timeout, signals, or process groups.

## `tty`

Host terminal plumbing only (no Kitty, mouse, colors, or alternate screen). Methods cannot attach to `fs.File` from another package, so the API is free functions:

```c
import "tty";

struct WinSize { int rows; int cols; };

bool tty_isatty(File *f);
bool tty_set_raw(File *f);      /* save attrs; enter raw */
bool tty_set_cooked(File *f);   /* restore attrs from set_raw */
(WinSize, bool) tty_size(File *f);
(File, bool) tty_stdin();   /* borrow fd/handle 0 */
(File, bool) tty_stdout();
(File, bool) tty_stderr();
```

- `tty_stdin` / `stdout` / `stderr` use `file_from_fd_borrow` (or Win32 handle borrow). `File.close` does not close the process stdio descriptors.
- `set_cooked` restores the attributes saved by a prior `set_raw` on the same fd/handle; without a save it returns false.
- Buffering stays in `fs`: compose with `writer_from_file` / `reader_from_file`. Flush before mode changes. `Poll` / `isatty` / `size` see the underlying `File` only.
