// path — slash-separated logical paths. No I/O.
//
// Paths use '/' only. path_to_sys / path_from_sys convert separators (and a
// leading drive letter) on Win32; they are identity elsewhere. Writers fill a
// caller buffer (cap(dst) is room) and return a view into that buffer.
// Slice helpers return views into the input. T[..] is opaque: use len/cap/ptr.
//
// On failure, if cap(dst) > 0 the first byte is set to NUL and no partial path
// is written. Success returns ranged(ptr(dst), n, cap(dst)) with a trailing
// NUL when one extra byte remains.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { PathMax = 4096 };

static bool path_slash(char c) {
	return c == '/';
}

static bool path_drive_letter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char[..] path_empty() {
	return ranged((char*)0, 0);
}

static(char[..], bool) path_put(char[..] dst, const char *src, size_t n) {
	if (cap(dst) < n) {
		if (cap(dst) > 0) {
			ptr(dst)[0] = 0;
		}
		return (path_empty(), false);
	}
	if (n > 0) {
		memcpy(ptr(dst), src, n);
	}
	if (cap(dst) > n) {
		ptr(dst)[n] = 0;
	}
	return (ranged(ptr(dst), n, cap(dst)), true);
}

// True when p starts with '/'. Drive letters are not absolute here.
bool path_is_abs(const char[..] p) {
	return len(p) > 0 && path_slash(p[0]);
}

// Directory of p as a view. "" if none; "/" if p is root.
const? char[..] path_dir(const? char[..] p) {
	size_t n = len(p);
	size_t end = 0;
	size_t i = 0;
	if (n == 0) {
		return path_empty();
	}
	end = n;
	while (end > 1 && path_slash(p[end - 1])) {
		end--;
	}
	if (end == 1 && path_slash(p[0])) {
		return p[0 .. 1];
	}
	i = end;
	while (i > 0 && !path_slash(p[i - 1])) {
		i--;
	}
	if (i == 0) {
		return path_empty();
	}
	if (i == 1) {
		return p[0 .. 1];
	}
	return p[0 ..(i - 1)];
}

// Final component as a view. Trailing slashes ignored. "/" for root.
const? char[..] path_base(const? char[..] p) {
	size_t n = len(p);
	size_t end = 0;
	size_t i = 0;
	if (n == 0) {
		return path_empty();
	}
	end = n;
	while (end > 0 && path_slash(p[end - 1])) {
		end--;
	}
	if (end == 0) {
		return p[0 .. 1];
	}
	i = end;
	while (i > 0 && !path_slash(p[i - 1])) {
		i--;
	}
	return p[i .. end];
}

// Trim trailing slashes. All-slash input becomes a single '/'.
static size_t path_trim_tail(const char[..] p, size_t n, int* root) {
	size_t end = n;
	*root = 0;
	while (end > 0 && path_slash(p[end - 1])) {
		end--;
	}
	if (end == 0 && n > 0) {
		*root = 1;
		return 0;
	}
	return end;
}

// Write a/b into dst. Returns a view of the written path, or empty on failure.
(char[..], bool) path_join(char[..] dst, const char[..] a, const char[..] b) {
	char tmp[PathMax] = { 0 };
	size_t an = 0;
	size_t bi = 0;
	size_t bt = 0;
	size_t n = 0;
	int aroot = 0;
	int babs = 0;
	an = path_trim_tail(a, len(a), &aroot);
	babs = path_is_abs(b) ? 1: 0;
	bi = 0;
	while (bi < len(b) && path_slash(b[bi])) {
		bi++;
	}
	bt = len(b) - bi;
	while (bt > 0 && path_slash(b[bi + bt - 1])) {
		bt--;
	}
	if (aroot) {
		tmp[n++] = '/';
	} else if (an > 0) {
		if (an > PathMax) {
			return (path_empty(), false);
		}
		memcpy(tmp, ptr(a), an);
		n = an;
	}
	if (bt == 0) {
		return path_put(dst, tmp, n);
	}
	if (n > 0 && !(n == 1 && tmp[0] == '/')) {
		if (n + 1 > PathMax) {
			return (path_empty(), false);
		}
		tmp[n++] = '/';
	} else if (n == 0 && babs) {
		tmp[n++] = '/';
	}
	if (bt > PathMax - n) {
		return (path_empty(), false);
	}
	memcpy(tmp + n, ptr(b) + bi, bt);
	n += bt;
	return path_put(dst, tmp, n);
}

// Collapse //, '.', and '..'. Returns a view of the cleaned path.
(char[..], bool) path_clean(char[..] dst, const char[..] p) {
	char tmp[PathMax] = { 0 };
	size_t n = 0;
	size_t i = 0;
	int abs = 0;
	abs = path_is_abs(p) ? 1: 0;
	if (abs) {
		tmp[n++] = '/';
	}
	while (i < len(p)) {
		size_t start = 0;
		size_t clen = 0;
		while (i < len(p) && path_slash(p[i])) {
			i++;
		}
		if (i >= len(p)) {
			break;
		}
		start = i;
		while (i < len(p) && !path_slash(p[i])) {
			i++;
		}
		clen = i - start;
		if (clen == 1 && p[start] == '.') {
			continue;
		}
		if (clen == 2 && p[start] == '.' && p[start + 1] == '.') {
			size_t floor = abs ? 1: 0;
			if (n <= floor) {
				continue;
			}
			if (tmp[n - 1] == '/') {
				n--;
			}
			while (n > floor && tmp[n - 1] != '/') {
				n--;
			}
			if (n > floor && tmp[n - 1] == '/') {
				n--;
			}
			continue;
		}
		if (n > (abs ?(size_t)1: 0)) {
			if (n + 1 > PathMax) {
				return (path_empty(), false);
			}
			tmp[n++] = '/';
		}
		if (clen > PathMax - n) {
			return (path_empty(), false);
		}
		memcpy(tmp + n, ptr(p) + start, clen);
		n += clen;
	}
	return path_put(dst, tmp, n);
}

// Host form of a logical path. Win32: '/' → '\\', and '/C:/…' → 'C:\…'.
(char[..], bool) path_to_sys(char[..] dst, const char[..] p) {
#ifdef _WIN32
	char tmp[PathMax] = { 0 };
	size_t i = 0;
	size_t n = 0;
	size_t o = 0;
	n = len(p);
	if (n > PathMax) {
		return (path_empty(), false);
	}
	if (n >= 3 && path_slash(p[0]) && path_drive_letter(p[1]) && p[2] == ':') {
		i = 1;
	}
	while (i < n) {
		char c = p[i];
		if (c == '/') {
			c = '\\';
		}
		tmp[o] = c;
		o++;
		i++;
	}
	return path_put(dst, tmp, o);
#else
	if (len(p) > PathMax) {
		return (path_empty(), false);
	}
	return path_put(dst, ptr(p), len(p));
#endif
}

// Logical form of a host path. Win32: '\\' → '/', and 'C:\…' → '/C:/…'.
(char[..], bool) path_from_sys(char[..] dst, const char[..] sys) {
#ifdef _WIN32
	char tmp[PathMax] = { 0 };
	size_t n = 0;
	size_t i = 0;
	size_t o = 0;
	int drive = 0;
	n = len(sys);
	drive = n >= 2 && path_drive_letter(sys[0]) && sys[1] == ':';
	if (n + (size_t)drive > PathMax) {
		return (path_empty(), false);
	}
	if (drive) {
		tmp[o] = '/';
		o++;
	}
	while (i < n) {
		char c = sys[i];
		if (c == '\\') {
			c = '/';
		}
		tmp[o] = c;
		o++;
		i++;
	}
	return path_put(dst, tmp, o);
#else
	return path_to_sys(dst, sys);
#endif
}
