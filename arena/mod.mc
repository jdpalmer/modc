// arena — bump region allocator; builds return char[..] into the region.
//
// Arena: one defer a.free() per scope; no per-string free. Reset with a.reset()
// to reuse the same arena without freeing backing storage.
//
// Methods copy/append/join/replace into the region; returned char[..] stay valid
// until a.free() or a.reset(). Byte semantics (same as str); not UTF-8
// validation. Grow with s = a.append(s, …) — assign a new header, do not mutate
// T[..] fields. Ownership of the bytes is the arena's (convention, like free(3)
// on a malloc'd char*).
//
// Receivers are live handles (see docs/methods.md). Do not null-check the
// receiver; callers must not call methods on a null Arena*.
import "str";

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct Arena {
	char* base;
	size_t cap;
	size_t off;
};

static bool arena_ensure(Arena* a, size_t need) {
	char* p = { 0 };
	size_t newcap = { 0 };
	if (need <= a.cap) {
		return true;
	}
	if (a.cap != 0) {
		newcap = a.cap;
	} else {
		newcap = 4096;
	}
	while (newcap < need) {
		newcap = newcap * 2;
	}
	p = realloc(a.base, newcap);
	if (p == NULL) {
		return false;
	}
	a.base = p;
	a.cap = newcap;
	return true;
}

static char* arena_bump(Arena* a, size_t n) {
	if (n == 0) {
		return NULL;
	}
	if (!arena_ensure(a, a.off + n)) {
		return NULL;
	}
	{
		char* p = a.base + a.off;
		a.off = a.off + n;
		return p;
	}
}

// Zero an arena. Call before first use.
void (Arena* a).init() {
	a.base = NULL;
	a.cap = 0;
	a.off = 0;
}

// Free backing storage. Receiver must be live (see docs/methods.md).
void (Arena* a).free() {
	free(a.base);
	a.base = NULL;
	a.cap = 0;
	a.off = 0;
}

// Keep backing storage; next allocations reuse from offset 0.
void (Arena* a).reset() {
	a.off = 0;
}

// Copy a view into the arena.
(bool, char[..]) (Arena* a).copy(const char[..] s) {
	size_t n = { 0 };
	char* p = { 0 };
	n = len(s);
	if (n == 0) {
		return (true, str_empty());
	}
	p = arena_bump(a, n);
	if (p == NULL) {
		return (false, str_empty());
	}
	memcpy(p, ptr(s), n);
	return (true, ranged(p, n));
}

// Append s onto cur; copies into the arena. Returns a new view (assign it).
(bool, char[..]) (Arena* a).append(char[..] cur, const char[..] s) {
	size_t cn = { 0 };
	size_t n = { 0 };
	size_t newlen = { 0 };
	char* p = { 0 };
	cn = len(cur);
	n = len(s);
	newlen = cn + n;
	if (newlen < cn) {
		return (false, str_empty());
	}
	if (newlen == 0) {
		return (true, str_empty());
	}
	if (n == 0) {
		return (true, cur);
	}
	p = arena_bump(a, newlen);
	if (p == NULL) {
		return (false, str_empty());
	}
	if (cn != 0) {
		memcpy(p, ptr(cur), cn);
	}
	memcpy(p + cn, ptr(s), n);
	return (true, ranged(p, newlen));
}

// Append one byte onto cur.
(bool, char[..]) (Arena* a).append_byte(char[..] cur, char c) {
	char one[1] = { 0 };
	one[0] = c;
	return a.append(cur, ranged(one, 1));
}

// Join parts with sep into the arena.
(bool, char[..]) (Arena* a).join(const char[..] sep, const char[..]* parts, size_t nparts) {
	char[..] out = { 0 };
	size_t i = { 0 };
	out = str_empty();
	for (i = 0; i < nparts; i++) {
		if (i != 0) {
			auto (ok, next) = a.append(out, sep);
			if (!ok) {
				return (false, str_empty());
			}
			out = next;
		}
		{
			auto (ok, next) = a.append(out, parts[i]);
			if (!ok) {
				return (false, str_empty());
			}
			out = next;
		}
	}
	return (true, out);
}

// Replace every occurrence of old with new.
(bool, char[..]) (Arena* a).replace(const char[..] s, const char[..] old, const char[..] new) {
	char[..] out = { 0 };
	size_t i = { 0 };
	if (len(old) == 0) {
		return a.copy(s);
	}
	out = str_empty();
	i = 0;
	while (i < len(s)) {
		const char[..] tail = s[i ..];
		auto (ok, hit) = str_find(tail, old);
		size_t off = { 0 };
		if (!ok) {
			auto (put_ok, next) = a.append(out, tail);
			if (!put_ok) {
				return (false, str_empty());
			}
			return (true, next);
		}
		off = ptr(hit) - ptr(tail);
		{
			auto (put_ok, next) = a.append(out, tail[0 .. off]);
			if (!put_ok) {
				return (false, str_empty());
			}
			out = next;
		}
		{
			auto (put_ok, next) = a.append(out, new);
			if (!put_ok) {
				return (false, str_empty());
			}
			out = next;
		}
		i = i + off + len(old);
	}
	return (true, out);
}

// NUL-terminated copy of s in the arena (C boundary).
char* (Arena* a).z(const char[..] s) {
	size_t n = { 0 };
	char* p = { 0 };
	n = len(s);
	p = arena_bump(a, n + 1);
	if (p == NULL) {
		return NULL;
	}
	if (n != 0) {
		memcpy(p, ptr(s), n);
	}
	p[n] = '\0';
	return p;
}
