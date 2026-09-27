// Win32 helpers for fs (package-internal).
#ifdef _WIN32
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Win32 File/Dir wrappers for fs. Decls and layouts come from <windows.h>.
   Kind values match KindFile / KindDir / KindOther. */

enum {
	FsWinShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	FsWinBackup = FILE_FLAG_BACKUP_SEMANTICS,
	FsWinAttrDir = FILE_ATTRIBUTE_DIRECTORY,
	FsWinNoMore = ERROR_NO_MORE_FILES,
	FsWinBadAttr = INVALID_FILE_ATTRIBUTES
};

struct FsWinFile {
	HANDLE h;
	int flags;
	int owns;
};

/* Same layout as WIN32_FIND_DATAA / BY_HANDLE_FILE_INFORMATION. */
struct FsWinFind {
	unsigned attr;
	unsigned ctime_lo;
	unsigned ctime_hi;
	unsigned atime_lo;
	unsigned atime_hi;
	unsigned mtime_lo;
	unsigned mtime_hi;
	unsigned size_hi;
	unsigned size_lo;
	unsigned reserved0;
	unsigned reserved1;
	char name[260];
	char alt[14];
};

struct FsWinInfo {
	unsigned attr;
	unsigned ctime_lo;
	unsigned ctime_hi;
	unsigned atime_lo;
	unsigned atime_hi;
	unsigned mtime_lo;
	unsigned mtime_hi;
	unsigned vol;
	unsigned size_hi;
	unsigned size_lo;
	unsigned nlink;
	unsigned idx_hi;
	unsigned idx_lo;
};

struct FsWinDir {
	HANDLE hf;
	int eof;
	int have;
	/* name[] holds an entry not yet returned */
	char name[260];
};

int fs_win_bad(HANDLE h) {
	return h == 0 || h == INVALID_HANDLE_VALUE;
}

int64_t fs_win_mtime(unsigned lo, unsigned hi) {
	uint64_t ticks = 0;
	uint64_t epoch = 116444736000000000ul;
	ticks = ((uint64_t)hi << 32) | (uint64_t)lo;
	if (ticks < epoch) {
		return 0;
	}
	return ((ticks - epoch)* 100ul);
}

void* fs_win_from_handle(HANDLE h, int owns) {
	FsWinFile* blob = 0;
	if (fs_win_bad(h)) {
		return 0;
	}
	blob = (FsWinFile *)malloc(sizeof(*blob));
	if (blob == 0) {
		return 0;
	}
	blob.h = h;
	blob.flags = 1 | 2;
	/* read|write */
	blob.owns = owns ? 1: 0;
	return blob;
}

HANDLE fs_win_handle(void* blob) {
	FsWinFile* f = 0;
	if (blob == 0) {
		return 0;
	}
	f = (FsWinFile *)blob;
	if (fs_win_bad(f.h)) {
		return 0;
	}
	return f.h;
}

void* fs_win_open(char* path, int flags) {
	FsWinFile* blob = 0;
	HANDLE h = 0;
	DWORD access = 0;
	DWORD disp = 0;
	int rd = 0;
	int wr = 0;
	if (path == 0 || path[0] == 0) {
		return 0;
	}
	rd = (flags & 1) != 0;
	wr = (flags & 2) != 0;
	if ((flags & 4) != 0) {
		wr = 1;
	}
	if (!rd && !wr) {
		return 0;
	}
	access = 0;
	if (rd) {
		access |= GENERIC_READ;
	}
	if (wr) {
		access |= GENERIC_WRITE;
	}
	if ((flags & 16) != 0 && wr && (flags & 4) != 0) {
		disp = CREATE_ALWAYS;
	} else if ((flags & 16) != 0 && wr) {
		disp = TRUNCATE_EXISTING;
	} else if ((flags & 4) != 0) {
		disp = OPEN_ALWAYS;
	} else {
		disp = OPEN_EXISTING;
	}
	h = CreateFileA(path, access, FsWinShare, 0, disp, FILE_ATTRIBUTE_NORMAL, 0);
	if (fs_win_bad(h)) {
		return 0;
	}
	blob = (FsWinFile *)malloc(sizeof(*blob));
	if (blob == 0) {
		CloseHandle(h);
		return 0;
	}
	blob.h = h;
	blob.flags = flags;
	blob.owns = 1;
	return blob;
}

void fs_win_close(void* blob) {
	FsWinFile* f = 0;
	if (blob == 0) {
		return;
	}
	f = (FsWinFile *)blob;
	if (f.owns && !fs_win_bad(f.h)) {
		CloseHandle(f.h);
	}
	free(blob);
}

int fs_win_read(void* blob, void* buf, size_t n, int64_t* nout) {
	FsWinFile* f = 0;
	unsigned got = 0;
	unsigned ask = 0;
	if (blob == 0 || nout == 0) {
		return 0;
	}
	f = (FsWinFile *)blob;
	if (fs_win_bad(f.h)) {
		return 0;
	}
	if (n == 0) {
		*nout = 0;
		return 1;
	}
	if (buf == 0) {
		return 0;
	}
	ask = n > 0x7fffffffu ? 0x7fffffffu:(unsigned)n;
	got = 0;
	if (!ReadFile(f.h, buf, ask, &got, 0)) {
		return 0;
	}
	*nout = got;
	return 1;
}

int fs_win_write(void* blob, void* buf, size_t n, int64_t* nout) {
	FsWinFile* f = 0;
	unsigned got = 0;
	unsigned ask = 0;
	if (blob == 0 || nout == 0) {
		return 0;
	}
	f = (FsWinFile *)blob;
	if (fs_win_bad(f.h)) {
		return 0;
	}
	if ((f.flags & 8) != 0) {
		if (!SetFilePointerEx(f.h, 0, 0, FILE_END)) {
			return 0;
		}
	}
	if (n == 0) {
		*nout = 0;
		return 1;
	}
	if (buf == 0) {
		return 0;
	}
	ask = n > 0x7fffffffu ? 0x7fffffffu:(unsigned)n;
	got = 0;
	if (!WriteFile(f.h, buf, ask, &got, 0)) {
		return 0;
	}
	*nout = got;
	return 1;
}

int fs_win_seek(void* blob, int64_t off, int whence, int64_t* npos) {
	FsWinFile* f = 0;
	unsigned method = 0;
	int64_t neu = 0;
	if (blob == 0 || npos == 0) {
		return 0;
	}
	f = (FsWinFile *)blob;
	if (fs_win_bad(f.h)) {
		return 0;
	}
	if (whence == 1) {
		method = FILE_CURRENT;
	} else if (whence == 2) {
		method = FILE_END;
	} else if (whence == 0) {
		method = FILE_BEGIN;
	} else {
		return 0;
	}
	neu = 0;
	if (!SetFilePointerEx(f.h, off, &neu, method)) {
		return 0;
	}
	*npos = neu;
	return 1;
}

int fs_win_remove(char* path) {
	if (path == 0 || path[0] == 0) {
		return 0;
	}
	if (DeleteFileA(path)) {
		return 1;
	}
	return RemoveDirectoryA(path) != 0;
}

int fs_win_rename(char* from, char* to) {
	if (from == 0 || to == 0) {
		return 0;
	}
	/* MOVEFILE_REPLACE_EXISTING: match POSIX rename overwrite. */
	return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
}

int fs_win_mkdir(char* path) {
	if (path == 0 || path[0] == 0) {
		return 0;
	}
	return CreateDirectoryA(path, 0) != 0;
}

int fs_win_stat(char* path, int* kind, int64_t* size, int64_t* mtime) {
	struct FsWinInfo info = { 0 };
	HANDLE h = 0;
	DWORD typ = 0;
	if (path == 0 || kind == 0 || size == 0 || mtime == 0) {
		return 0;
	}
	*kind = 0;
	*size = 0;
	*mtime = 0;
	h = CreateFileA(path, 0, FsWinShare, 0, OPEN_EXISTING, FsWinBackup, 0);
	if (fs_win_bad(h)) {
		return 0;
	}
	typ = GetFileType(h);
	if (typ == FILE_TYPE_DISK) {
		memset(&info, 0, sizeof(info));
		if (!GetFileInformationByHandle(h, (BY_HANDLE_FILE_INFORMATION *)& info)) {
			CloseHandle(h);
			return 0;
		}
		*mtime = fs_win_mtime(info.mtime_lo, info.mtime_hi);
		if ((info.attr & FsWinAttrDir) != 0) {
			*kind = 2;
		} else {
			*kind = 1;
			*size = ((int64_t)info.size_hi << 32) | (int64_t)info.size_lo;
		}
	} else if (typ == FILE_TYPE_CHAR || typ == FILE_TYPE_PIPE) {
		*kind = 3;
	} else {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	return 1;
}

int fs_win_dot(char* name) {
	if (name == 0) {
		return 1;
	}
	if (name[0] == '.' && name[1] == 0) {
		return 1;
	}
	if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
		return 1;
	}
	return 0;
}

void* fs_win_diropen(char* path) {
	FsWinDir* blob = 0;
	struct FsWinFind ent = { 0 };
	char pat[4100] = { 0 };
	size_t n = 0;
	DWORD attr = 0;
	HANDLE hf = 0;
	if (path == 0 || path[0] == 0) {
		return 0;
	}
	attr = GetFileAttributesA(path);
	if (attr == FsWinBadAttr || (attr & FsWinAttrDir) == 0) {
		return 0;
	}
	n = strlen(path);
	if (n + 3 >= sizeof(pat)) {
		return 0;
	}
	memcpy(pat, path, n);
	if (pat[n - 1] != '\\') {
		pat[n++] = '\\';
	}
	pat[n++] = '*';
	pat[n] = 0;
	memset(&ent, 0, sizeof(ent));
	hf = FindFirstFileA(pat, (WIN32_FIND_DATAA *)& ent);
	if (fs_win_bad(hf)) {
		return 0;
	}
	blob = (FsWinDir *)malloc(sizeof(*blob));
	if (blob == 0) {
		FindClose(hf);
		return 0;
	}
	blob.hf = hf;
	blob.eof = 0;
	blob.have = 0;
	blob.name[0] = 0;
	/* Prime with the first non-dot name, or mark end. */
	for (;;) {
		size_t ln = 0;
		if (!fs_win_dot(ent.name)) {
			ln = strlen(ent.name);
			if (ln >= sizeof(blob.name)) {
				FindClose(hf);
				free(blob);
				return 0;
			}
			memcpy(blob.name, ent.name, ln + 1);
			blob.have = 1;
			return blob;
		}
		memset(&ent, 0, sizeof(ent));
		if (!FindNextFileA(hf, (WIN32_FIND_DATAA *)& ent)) {
			if (GetLastError() == FsWinNoMore) {
				blob.eof = 1;
			} else {
				FindClose(hf);
				free(blob);
				return 0;
			}
			return blob;
		}
	}
}

/* 1 = *out points into the blob (valid until the next next/close),
   0 = end, -1 = error. */
int fs_win_next(void* blob, char** out, size_t* nlen) {
	FsWinDir* d = 0;
	struct FsWinFind ent = { 0 };
	size_t n = 0;
	if (blob == 0 || out == 0 || nlen == 0) {
		return -1;
	}
	d = (FsWinDir *)blob;
	*out = 0;
	*nlen = 0;
	if (d.have) {
		n = strlen(d.name);
		*out = d.name;
		*nlen = n;
		d.have = 0;
		return 1;
	}
	if (d.eof || fs_win_bad(d.hf)) {
		if (d.eof) {
			return 0;
		}
		return -1;
	}
	for (;;) {
		memset(&ent, 0, sizeof(ent));
		if (!FindNextFileA(d.hf, (WIN32_FIND_DATAA *)& ent)) {
			if (GetLastError() == FsWinNoMore) {
				d.eof = 1;
				return 0;
			}
			return -1;
		}
		if (fs_win_dot(ent.name)) {
			continue;
		}
		n = strlen(ent.name);
		if (n >= sizeof(d.name)) {
			return -1;
		}
		memcpy(d.name, ent.name, n + 1);
		*out = d.name;
		*nlen = n;
		return 1;
	}
}

void fs_win_dirclose(void* blob) {
	FsWinDir* d = 0;
	if (blob == 0) {
		return;
	}
	d = (FsWinDir *)blob;
	if (!fs_win_bad(d.hf)) {
		FindClose(d.hf);
	}
	free(blob);
}
#endif
