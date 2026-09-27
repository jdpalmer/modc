// fs — file handles. Not POSIX and not Win32.
//
// File owns a heap blob behind static native. NULL means closed. An open fd
// of 0 is stored inside the blob, never as the pointer itself. Do not copy
// File by value. Zero-init is closed. close is idempotent. file_from_fd /
// file_from_handle take ownership (close closes the host handle).
// file_from_fd_borrow / file_from_handle_borrow do not; close only frees the
// blob (for stdio and other borrowed descriptors).
//
// Null / closed receivers: unlike ordinary %C methods (live handles), File and
// related I/O methods treat a null File* or native == NULL as a documented
// failure (or a no-op for close). Do not copy that pattern onto normal types.
//
// read:  (0, true) is EOF; (0, false) is error; short reads are success.
// write: (n, true) may be short; callers loop. (0, false) is error.
// OpenCreate implies write. OpenAppend seeks to end before each write.
// OpenTrunc truncates when opening for write.
//
// Writer / Reader buffer on File. writer_open / reader_open own the File;
// writer_from_file / reader_from_file borrow. Writer flush is UTF-8-safe
// (holds a 1–3 byte incomplete trail). close flush-all then, if owned,
// File.close. Do not mix raw File I/O while a Writer/Reader is live. Poll
// sees only the underlying File (buffered data is not visible). cap 0 uses
// DefaultBufCap; cap must be 0 or >= 4.
//
// fs_stat follows symlinks. Missing and dangling are !ok. KindOther is
// anything that exists and is neither a regular file nor a directory.
// Dir.next returns a view into the handle, valid until the next next or close.
// A non-empty view and true is an entry. Empty and true is the end.
// Empty and false is an error. "." and ".." are skipped.
import "path";
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	OpenRead = 1,
	OpenWrite = 2,
	OpenCreate = 4,
	OpenAppend = 8,
	OpenTrunc = 16
};

enum {
	SeekSet = 0,
	SeekCur = 1,
	SeekEnd = 2
};

enum { FsPathMax = 4096 };

struct File {
	static void* native;
};

enum {
	KindFile = 1,
	KindDir = 2,
	KindOther = 3
};

struct FileInfo {
	int kind;
	int64_t size;
	int64_t mtime_ns;
};

struct Dir {
	static void* native;
};

enum { DefaultBufCap = 4096 };

struct Writer {
	static void* native;
};

struct Reader {
	static void* native;
};

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

// Host path in buf, NUL-terminated. Logical '/' paths become '\\' on Win32.
static int fs_syspath(const char[..] path, char* buf, size_t cap) {
	if (cap == 0 || (len(path) > 0 && ptr(path) == NULL)) {
		return 0;
	}
	{
		auto (view, ok) = path_to_sys(ranged(buf, 0, cap), path);
		if (!ok || len(view) >= cap) {
			return 0;
		}
	}
	return 1;
}

#ifndef _WIN32
struct FsFd {
	int fd;
	int owns;
};

static int fs_fd(File* f) {
	FsFd* blob = NULL;
	if (f == NULL || f.native == NULL) {
		return -1;
	}
	blob = (FsFd *)f.native;
	return blob.fd;
}

static int fs_oflags(int flags) {
	int rd = (flags & OpenRead) != 0;
	int wr = (flags & OpenWrite) != 0;
	int acc = 0;
	if ((flags & OpenCreate) != 0) {
		wr = 1;
	}
	if (!rd && !wr) {
		return -1;
	}
	if (rd && wr) {
		acc = O_RDWR;
	} else if (wr) {
		acc = O_WRONLY;
	} else {
		acc = O_RDONLY;
	}
	if ((flags & OpenCreate) != 0) {
		acc |= O_CREAT;
	}
	if ((flags & OpenAppend) != 0) {
		acc |= O_APPEND;
	}
	if ((flags & OpenTrunc) != 0 && wr) {
		acc |= O_TRUNC;
	}
	return acc;
}
#endif

(File, bool) file_open(const char[..] path, int flags) {
	File f = { 0 };
#ifdef _WIN32
	char zbuf[FsPathMax] = { 0 };
	void* blob = NULL;
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return (f, false);
	}
	blob = fs_win_open(zbuf, flags);
	if (blob == NULL) {
		return (f, false);
	}
	f.native = blob;
	return (f, true);
#else
	char zbuf[FsPathMax] = { 0 };
	int ofl = 0;
	int fd = 0;
	FsFd* blob = NULL;
	ofl = fs_oflags(flags);
	if (ofl < 0 || !fs_syspath(path, zbuf, FsPathMax)) {
		return (f, false);
	}
	fd = open(zbuf, ofl, 438);
	if (fd < 0) {
		return (f, false);
	}
	blob = (FsFd *)malloc(sizeof(FsFd));
	if (blob == NULL) {
		close(fd);
		return (f, false);
	}
	blob.fd = fd;
	blob.owns = 1;
	f.native = blob;
	return (f, true);
#endif
}

// Take ownership of an existing fd. On success, File.close closes the fd. fd < 0 fails.
(File, bool) file_from_fd(int fd) {
	File f = { 0 };
#ifdef _WIN32
	(void)fd;
	return (f, false);
#else
	FsFd* blob = NULL;
	if (fd < 0) {
		return (f, false);
	}
	blob = (FsFd *)malloc(sizeof(FsFd));
	if (blob == NULL) {
		return (f, false);
	}
	blob.fd = fd;
	blob.owns = 1;
	f.native = blob;
	return (f, true);
#endif
}

// Wrap an existing fd without taking ownership. File.close frees the blob only.
(File, bool) file_from_fd_borrow(int fd) {
	File f = { 0 };
#ifdef _WIN32
	(void)fd;
	return (f, false);
#else
	FsFd* blob = NULL;
	if (fd < 0) {
		return (f, false);
	}
	blob = (FsFd *)malloc(sizeof(FsFd));
	if (blob == NULL) {
		return (f, false);
	}
	blob.fd = fd;
	blob.owns = 0;
	f.native = blob;
	return (f, true);
#endif
}

// Take ownership of a Win32 HANDLE. On success, File.close closes it.
(File, bool) file_from_handle(void* h) {
	File f = { 0 };
#ifdef _WIN32
	void* blob = NULL;
	if (h == NULL || h == (void*)(intptr_t) - 1) {
		return (f, false);
	}
	blob = fs_win_from_handle(h, 1);
	if (blob == NULL) {
		return (f, false);
	}
	f.native = blob;
	return (f, true);
#else
	(void)h;
	return (f, false);
#endif
}

// Wrap a Win32 HANDLE without taking ownership. File.close frees the blob only.
(File, bool) file_from_handle_borrow(void* h) {
	File f = { 0 };
#ifdef _WIN32
	void* blob = NULL;
	if (h == NULL || h == (void*)(intptr_t) - 1) {
		return (f, false);
	}
	blob = fs_win_from_handle(h, 0);
	if (blob == NULL) {
		return (f, false);
	}
	f.native = blob;
	return (f, true);
#else
	(void)h;
	return (f, false);
#endif
}

// Underlying fd for poll/spawn helpers. -1 if closed or not a POSIX File.
int file_fd(File* f) {
#ifdef _WIN32
	(void)f;
	return -1;
#else
	return fs_fd(f);
#endif
}

// Underlying HANDLE for poll/spawn helpers. NULL if closed or not a Win32 File.
void* file_handle(File* f) {
#ifdef _WIN32
	if (f == NULL || f.native == NULL) {
		return NULL;
	}
	return fs_win_handle(f.native);
#else
	(void)f;
	return NULL;
#endif
}

(int64_t, bool) (File* f).read(char[..] dst) {
#ifdef _WIN32
	int64_t n = 0;
	if (f.native == NULL) {
		return (0, false);
	}
	if (cap(dst) == 0) {
		return (0, true);
	}
	if (!fs_win_read(f.native, ptr(dst), cap(dst), &n)) {
		return (0, false);
	}
	return (n, true);
#else
	int fd = fs_fd(f);
	ssize_t n = 0;
	if (fd < 0) {
		return (0, false);
	}
	if (cap(dst) == 0) {
		return (0, true);
	}
	n = read(fd, ptr(dst), cap(dst));
	if (n < 0) {
		return (0, false);
	}
	return (n, true);
#endif
}

(int64_t, bool) (File* f).write(const char[..] src) {
#ifdef _WIN32
	int64_t n = 0;
	if (f.native == NULL) {
		return (0, false);
	}
	if (len(src) == 0) {
		return (0, true);
	}
	if (!fs_win_write(f.native, ptr(src), len(src), &n)) {
		return (0, false);
	}
	return (n, true);
#else
	int fd = fs_fd(f);
	ssize_t n = 0;
	if (fd < 0) {
		return (0, false);
	}
	if (len(src) == 0) {
		return (0, true);
	}
	n = write(fd, ptr(src), len(src));
	if (n < 0) {
		return (0, false);
	}
	return (n, true);
#endif
}

(int64_t, bool) (File* f).seek(int64_t off, int whence) {
#ifdef _WIN32
	int64_t got = 0;
	if (f.native == NULL) {
		return (0, false);
	}
	if (whence != SeekSet && whence != SeekCur && whence != SeekEnd) {
		return (0, false);
	}
	if (!fs_win_seek(f.native, off, whence, &got)) {
		return (0, false);
	}
	return (got, true);
#else
	int fd = fs_fd(f);
	int wh = 0;
	off_t got = 0;
	if (fd < 0) {
		return (0, false);
	}
	if (whence == SeekCur) {
		wh = SEEK_CUR;
	} else if (whence == SeekEnd) {
		wh = SEEK_END;
	} else if (whence == SeekSet) {
		wh = SEEK_SET;
	} else {
		return (0, false);
	}
	got = lseek(fd, off, wh);
	if (got < 0) {
		return (0, false);
	}
	return (got, true);
#endif
}

void (File* f).close() {
#ifdef _WIN32
	if (f.native == NULL) {
		return;
	}
	fs_win_close(f.native);
	f.native = NULL;
#else
	FsFd* blob = NULL;
	if (f.native == NULL) {
		return;
	}
	blob = (FsFd *)f.native;
	if (blob.owns && blob.fd >= 0) {
		close(blob.fd);
	}
	free(f.native);
	f.native = NULL;
#endif
}

struct FsBufW {
	File owned;
	File* borrow;
	int owns;
	char* buf;
	size_t cap;
	size_t len;
};

struct FsBufR {
	File owned;
	File* borrow;
	int owns;
	char* buf;
	size_t cap;
	size_t off;
	size_t end;
};

static size_t fs_buf_cap(size_t cap) {
	if (cap == 0) {
		return DefaultBufCap;
	}
	return cap;
}

static size_t utf8_complete_prefix(const char *p, size_t n) {
	size_t i = 0;
	size_t trail = 0;
	size_t need = 0;
	char c = 0;
	if (n == 0) {
		return 0;
	}
	i = n;
	while (i > 0 && trail < 3 && (p[i - 1] & 0xc0) == 0x80) {
		trail = trail + 1;
		i = i - 1;
	}
	if (i == 0) {
		return 0;
	}
	c = p[i - 1];
	if (c < 0x80) {
		need = 1;
	} else if ((c & 0xe0) == 0xc0) {
		need = 2;
	} else if ((c & 0xf0) == 0xe0) {
		need = 3;
	} else if ((c & 0xf8) == 0xf0) {
		need = 4;
	} else {
		return i - 1;
	}
	if (n - (i - 1) >= need) {
		return n;
	}
	return i - 1;
}

static File* fs_bufw_file(FsBufW* b) {
	if (b.owns) {
		return &b.owned;
	}
	return b.borrow;
}

static File* fs_bufr_file(FsBufR* b) {
	if (b.owns) {
		return &b.owned;
	}
	return b.borrow;
}

static int fs_file_write_all(File* f, const char *p, size_t n) {
	size_t off = 0;
	if (f == NULL || (n > 0 && p == NULL)) {
		return 0;
	}
	while (off < n) {
		auto (wn, ok) = f.write(ranged(p + off, n - off));
		if (!ok || wn <= 0) {
			return 0;
		}
		off = off + (size_t)wn;
	}
	return 1;
}

static int fs_bufw_flush_safe(FsBufW* b) {
	File* f = NULL;
	size_t n = 0;
	if (b == NULL || b.buf == NULL) {
		return 0;
	}
	f = fs_bufw_file(b);
	if (f == NULL) {
		return 0;
	}
	n = utf8_complete_prefix(b.buf, b.len);
	if (n == 0) {
		return 1;
	}
	if (!fs_file_write_all(f, b.buf, n)) {
		return 0;
	}
	if (n < b.len) {
		memmove(b.buf, b.buf + n, b.len - n);
	}
	b.len = b.len - n;
	return 1;
}

static int fs_bufw_flush_all(FsBufW* b) {
	File* f = NULL;
	if (b == NULL || b.buf == NULL) {
		return 0;
	}
	if (b.len == 0) {
		return 1;
	}
	f = fs_bufw_file(b);
	if (f == NULL) {
		return 0;
	}
	if (!fs_file_write_all(f, b.buf, b.len)) {
		return 0;
	}
	b.len = 0;
	return 1;
}

static int fs_bufw_setup(FsBufW* b, File* f, int owns, size_t cap) {
	char* buf = NULL;
	size_t c = fs_buf_cap(cap);
	if (b == NULL || f == NULL || f.native == NULL) {
		return 0;
	}
	if (cap != 0 && cap < 4) {
		return 0;
	}
	buf = (char*)malloc(c);
	if (buf == NULL) {
		return 0;
	}
	b.owned.native = NULL;
	b.borrow = NULL;
	b.owns = owns;
	if (owns) {
		b.owned.native = f.native;
		f.native = NULL;
	} else {
		b.borrow = f;
	}
	b.buf = buf;
	b.cap = c;
	b.len = 0;
	return 1;
}

static int fs_bufr_setup(FsBufR* b, File* f, int owns, size_t cap) {
	char* buf = NULL;
	size_t c = fs_buf_cap(cap);
	if (b == NULL || f == NULL || f.native == NULL) {
		return 0;
	}
	if (cap != 0 && cap < 4) {
		return 0;
	}
	buf = (char*)malloc(c);
	if (buf == NULL) {
		return 0;
	}
	b.owned.native = NULL;
	b.borrow = NULL;
	b.owns = owns;
	if (owns) {
		b.owned.native = f.native;
		f.native = NULL;
	} else {
		b.borrow = f;
	}
	b.buf = buf;
	b.cap = c;
	b.off = 0;
	b.end = 0;
	return 1;
}

(Writer, bool) writer_open(const char[..] path, int flags, size_t cap) {
	Writer w = { 0 };
	FsBufW* blob = NULL;
	File f = { 0 };
	{
		auto (of, ok) = file_open(path, flags);
		if (!ok) {
			return (w, false);
		}
		f = of;
	}
	blob = (FsBufW *)malloc(sizeof(FsBufW));
	if (blob == NULL) {
		f.close();
		return (w, false);
	}
	if (!fs_bufw_setup(blob, &f, 1, cap)) {
		f.close();
		free(blob);
		return (w, false);
	}
	w.native = blob;
	return (w, true);
}

(Writer, bool) writer_from_file(File* f, size_t cap) {
	Writer w = { 0 };
	FsBufW* blob = NULL;
	if (f == NULL || f.native == NULL) {
		return (w, false);
	}
	blob = (FsBufW *)malloc(sizeof(FsBufW));
	if (blob == NULL) {
		return (w, false);
	}
	if (!fs_bufw_setup(blob, f, 0, cap)) {
		free(blob);
		return (w, false);
	}
	w.native = blob;
	return (w, true);
}

(int64_t, bool) (Writer* w).write(const char[..] src) {
	FsBufW* b = NULL;
	File* f = NULL;
	const char *p = NULL;
	size_t want = 0;
	size_t done = 0;
	if (w.native == NULL) {
		return (0, false);
	}
	b = (FsBufW *)w.native;
	f = fs_bufw_file(b);
	if (f == NULL || f.native == NULL || b.buf == NULL) {
		return (0, false);
	}
	want = len(src);
	if (want == 0) {
		return (0, true);
	}
	p = ptr(src);
	if (p == NULL) {
		return (0, false);
	}
	while (done < want) {
		size_t rem = want - done;
		if (b.len < 4 && rem >= b.cap) {
			if (b.len > 0) {
				if (!fs_bufw_flush_safe(b)) {
					return (done, false);
				}
			}
			if (b.len > 0) {
				size_t room = b.cap - b.len;
				size_t take = rem < room ? rem: room;
				memcpy(b.buf + b.len, p + done, take);
				b.len = b.len + take;
				done = done + take;
				if (!fs_bufw_flush_safe(b)) {
					return (done, false);
				}
				continue;
			}
			{
				size_t emit = utf8_complete_prefix(p + done, rem);
				if (emit == 0) {
					memcpy(b.buf, p + done, rem);
					b.len = rem;
					done = want;
					break;
				}
				if (!fs_file_write_all(f, p + done, emit)) {
					return (done, false);
				}
				done = done + emit;
				rem = want - done;
				if (rem > 0 && rem < 4) {
					memcpy(b.buf, p + done, rem);
					b.len = rem;
					done = want;
				}
			}
			continue;
		}
		{
			size_t room = b.cap - b.len;
			size_t take = 0;
			if (room == 0) {
				if (!fs_bufw_flush_safe(b)) {
					return (done, false);
				}
				if (b.len >= b.cap) {
					return (done, false);
				}
				room = b.cap - b.len;
			}
			take = rem < room ? rem: room;
			memcpy(b.buf + b.len, p + done, take);
			b.len = b.len + take;
			done = done + take;
			if (b.len == b.cap) {
				if (!fs_bufw_flush_safe(b)) {
					return (done, false);
				}
			}
		}
	}
	return (done, true);
}

bool (Writer* w).flush() {
	FsBufW* b = NULL;
	if (w.native == NULL) {
		return false;
	}
	b = (FsBufW *)w.native;
	return fs_bufw_flush_safe(b) != 0;
}

void (Writer* w).close() {
	FsBufW* b = NULL;
	if (w.native == NULL) {
		return;
	}
	b = (FsBufW *)w.native;
	(void)fs_bufw_flush_all(b);
	if (b.owns) {
		b.owned.close();
	}
	free(b.buf);
	free(b);
	w.native = NULL;
}

(Reader, bool) reader_open(const char[..] path, int flags, size_t cap) {
	Reader r = { 0 };
	FsBufR* blob = NULL;
	File f = { 0 };
	{
		auto (of, ok) = file_open(path, flags);
		if (!ok) {
			return (r, false);
		}
		f = of;
	}
	blob = (FsBufR *)malloc(sizeof(FsBufR));
	if (blob == NULL) {
		f.close();
		return (r, false);
	}
	if (!fs_bufr_setup(blob, &f, 1, cap)) {
		f.close();
		free(blob);
		return (r, false);
	}
	r.native = blob;
	return (r, true);
}

(Reader, bool) reader_from_file(File* f, size_t cap) {
	Reader r = { 0 };
	FsBufR* blob = NULL;
	if (f == NULL || f.native == NULL) {
		return (r, false);
	}
	blob = (FsBufR *)malloc(sizeof(FsBufR));
	if (blob == NULL) {
		return (r, false);
	}
	if (!fs_bufr_setup(blob, f, 0, cap)) {
		free(blob);
		return (r, false);
	}
	r.native = blob;
	return (r, true);
}

(int64_t, bool) (Reader* r).read(char[..] dst) {
	FsBufR* b = NULL;
	File* f = NULL;
	char* out = NULL;
	size_t want = 0;
	size_t got = 0;
	if (r.native == NULL) {
		return (0, false);
	}
	b = (FsBufR *)r.native;
	f = fs_bufr_file(b);
	if (f == NULL || f.native == NULL || b.buf == NULL) {
		return (0, false);
	}
	want = cap(dst);
	if (want == 0) {
		return (0, true);
	}
	out = ptr(dst);
	if (out == NULL) {
		return (0, false);
	}
	while (got < want) {
		size_t avail = 0;
		size_t take = 0;
		if (b.off >= b.end) {
			auto (n, ok) = f.read(ranged(b.buf, 0, b.cap));
			if (!ok) {
				if (got > 0) {
					return (got, true);
				}
				return (0, false);
			}
			if (n == 0) {
				return (got, true);
			}
			b.off = 0;
			b.end = n;
		}
		avail = b.end - b.off;
		take = avail < (want - got) ? avail:(want - got);
		memcpy(out + got, b.buf + b.off, take);
		b.off = b.off + take;
		got = got + take;
	}
	return (got, true);
}

void (Reader* r).close() {
	FsBufR* b = NULL;
	if (r.native == NULL) {
		return;
	}
	b = (FsBufR *)r.native;
	if (b.owns) {
		b.owned.close();
	}
	free(b.buf);
	free(b);
	r.native = NULL;
}

bool fs_remove(const char[..] path) {
#ifdef _WIN32
	char zbuf[FsPathMax] = { 0 };
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return false;
	}
	return fs_win_remove(zbuf) != 0;
#else
	char zbuf[FsPathMax] = { 0 };
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return false;
	}
	if (unlink(zbuf) == 0) {
		return true;
	}
	/* Darwin unlink(dir) is EPERM, not EISDIR. Empty directories use rmdir. */
	if (rmdir(zbuf) == 0) {
		return true;
	}
	return false;
#endif
}

bool fs_rename(const char[..] from, const char[..] to) {
#ifdef _WIN32
	char zfrom[FsPathMax] = { 0 };
	char zto[FsPathMax] = { 0 };
	if (!fs_syspath(from, zfrom, FsPathMax) || !fs_syspath(to, zto, FsPathMax)) {
		return false;
	}
	return fs_win_rename(zfrom, zto) != 0;
#else
	char zfrom[FsPathMax] = { 0 };
	char zto[FsPathMax] = { 0 };
	if (!fs_syspath(from, zfrom, FsPathMax) || !fs_syspath(to, zto, FsPathMax)) {
		return false;
	}
	return rename(zfrom, zto) == 0;
#endif
}

bool fs_mkdir(const char[..] path) {
#ifdef _WIN32
	char zbuf[FsPathMax] = { 0 };
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return false;
	}
	return fs_win_mkdir(zbuf) != 0;
#else
	char zbuf[FsPathMax] = { 0 };
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return false;
	}
	return mkdir(zbuf, 511) == 0;
#endif
}

static char[..] fs_empty() {
	return ranged((char*)0, 0);
}

#ifndef _WIN32
enum { DirNameMax = 1024 };

struct FsDir {
	void* dp;
	size_t nlen;
	char name[1024];
};

static int fs_dotname(char* name, size_t n) {
	if (n == 1 && name[0] == '.') {
		return 1;
	}
	if (n == 2 && name[0] == '.' && name[1] == '.') {
		return 1;
	}
	return 0;
}
#endif

(FileInfo, bool) fs_stat(const char[..] path) {
	FileInfo info = { 0 };
#ifdef _WIN32
	char zbuf[FsPathMax] = { 0 };
	int kind = 0;
	int64_t size = 0;
	int64_t mtime = 0;
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return (info, false);
	}
	if (!fs_win_stat(zbuf, &kind, &size, &mtime)) {
		return (info, false);
	}
	info.kind = kind;
	info.mtime_ns = mtime;
	if (kind == KindFile) {
		info.size = size;
	}
	return (info, true);
#else
	char zbuf[FsPathMax] = { 0 };
	int kind = 0;
	int64_t size = 0;
	int64_t mtime = 0;
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return (info, false);
	}
	if (!fs_sys_stat(zbuf, &kind, &size, &mtime)) {
		return (info, false);
	}
	info.kind = kind;
	info.mtime_ns = mtime;
	if (kind == KindFile) {
		info.size = size;
	}
	return (info, true);
#endif
}

(Dir, bool) dir_open(const char[..] path) {
	Dir d = { 0 };
#ifdef _WIN32
	char zbuf[FsPathMax] = { 0 };
	void* blob = NULL;
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return (d, false);
	}
	blob = fs_win_diropen(zbuf);
	if (blob == NULL) {
		return (d, false);
	}
	d.native = blob;
	return (d, true);
#else
	char zbuf[FsPathMax] = { 0 };
	void* dp = NULL;
	FsDir* blob = NULL;
	if (!fs_syspath(path, zbuf, FsPathMax)) {
		return (d, false);
	}
	dp = fs_sys_opendir(zbuf);
	if (dp == NULL) {
		return (d, false);
	}
	blob = (FsDir *)malloc(sizeof(FsDir));
	if (blob == NULL) {
		fs_sys_closedir(dp);
		return (d, false);
	}
	blob.dp = dp;
	blob.nlen = 0;
	blob.name[0] = 0;
	d.native = blob;
	return (d, true);
#endif
}

(char[..], bool) (Dir* d).next() {
#ifdef _WIN32
	char* name = NULL;
	size_t n = 0;
	int rc = 0;
	if (d.native == NULL) {
		return (fs_empty(), false);
	}
	rc = fs_win_next(d.native, &name, &n);
	if (rc == 0) {
		return (fs_empty(), true);
	}
	if (rc < 0 || name == NULL) {
		return (fs_empty(), false);
	}
	return (ranged(name, n), true);
#else
	FsDir* blob = NULL;
	int rc = 0;
	size_t n = 0;
	if (d.native == NULL) {
		return (fs_empty(), false);
	}
	blob = (FsDir *)d.native;
	if (blob.dp == NULL) {
		return (fs_empty(), false);
	}
	for (;;) {
		rc = fs_sys_readdir(blob.dp, blob.name, DirNameMax, &n);
		if (rc == 0) {
			return (fs_empty(), true);
		}
		if (rc < 0) {
			return (fs_empty(), false);
		}
		if (fs_dotname(blob.name, n)) {
			continue;
		}
		blob.nlen = n;
		return (ranged(blob.name, n), true);
	}
#endif
}

void (Dir* d).close() {
#ifdef _WIN32
	if (d.native == NULL) {
		return;
	}
	fs_win_dirclose(d.native);
	d.native = NULL;
#else
	FsDir* blob = NULL;
	if (d.native == NULL) {
		return;
	}
	blob = (FsDir *)d.native;
	if (blob.dp != NULL) {
		fs_sys_closedir(blob.dp);
	}
	free(blob);
	d.native = NULL;
#endif
}
