// arena — bump region allocator and U8 string building (byte spans in arena memory).
//
// Arena: one defer a.free() per scope; no per-string free. Reset with a.reset()
// to reuse the same arena without freeing backing storage.
//
// Arena methods copy/join/replace into the region; returns char[..] views valid
// until a.free() or a.reset(). Byte semantics (same as str); not UTF-8 validation.
//
// U8: incremental builder (anonymous char[..] embed); projects to char[..] for str_*.
//   u.begin(&a); u.put(chunk); str_eq(u, "…");
//
// Preconditions:
//   Arena* a — non-null on Arena methods except a.free() when a is NULL (like free(3)).
//   U8* u — non-null on U8 mutators; u.arena set by u.begin.
import "str";

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct Arena {
	char* base;
	size_t cap;
	size_t off;
};

struct U8 {
	Arena* arena;
	char[..];
};

static bool arena_ensure(Arena* a, size_t need) {
	char* p = {0};
	size_t newcap = {0};
	if (a == NULL) {
		return false;
	}
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
	if (a == NULL || n == 0) {
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
void (Arena* a).init(void) {
	if (a == NULL) {
		return;
	}
	a.base = NULL;
	a.cap = 0;
	a.off = 0;
}

// Free backing storage. A null receiver is a no-op (like free(3)).
void (Arena* a).free(void) {
	if (a == NULL) {
		return;
	}
	free(a.base);
	a.base = NULL;
	a.cap = 0;
	a.off = 0;
}

// Keep backing storage; next allocations reuse from offset 0.
void (Arena* a).reset(void) {
	if (a == NULL) {
		return;
	}
	a.off = 0;
}

// Bind a builder to an arena and clear the view.
void (U8* u).begin(Arena* a) {
	u.arena = a;
	u.ptr = NULL;
	u.len = 0;
}

// Append a view; copies into the arena. Returns false on OOM.
bool (U8* u).put(char[..] s) {
	Arena* a = {0};
	size_t n = {0};
	size_t newlen = {0};
	char* p = {0};
	if (u == NULL || u.arena == NULL) {
		return false;
	}
	a = u.arena;
	n = len(s);
	newlen = u.len + n;
	if (newlen < u.len) {
		return false;
	}
	if (newlen == 0) {
		return true;
	}
	p = arena_bump(a, newlen);
	if (p == NULL) {
		return false;
	}
	if (u.len != 0) {
		memcpy(p, u.ptr, u.len);
	}
	if (n != 0) {
		memcpy(p + u.len, s.ptr, n);
	}
	u.ptr = p;
	u.len = newlen;
	return true;
}

// Append one byte.
bool (U8* u).put_byte(char c) {
	char one[1] = {0};
	one[0] = c;
	return u.put(ranged(one, 1));
}

// Copy a view into the arena.
(bool, char[..]) (Arena* a).copy(char[..] s) {
	size_t n = {0};
	char* p = {0};
	if (a == NULL) {
		return (false, str_empty());
	}
	n = len(s);
	if (n == 0) {
		return (true, str_empty());
	}
	p = arena_bump(a, n);
	if (p == NULL) {
		return (false, str_empty());
	}
	memcpy(p, s.ptr, n);
	return (true, ranged(p, n));
}

// Join parts with sep into the arena.
(bool, char[..]) (Arena* a).join(char[..] sep, char[..] * parts, size_t nparts) {
	U8 u = {0};
	size_t i = {0};
	if (a == NULL) {
		return (false, str_empty());
	}
	u.begin(a);
	for (i = 0; i < nparts; i++) {
		if (i != 0) {
			if (!u.put(sep)) {
				return (false, str_empty());
			}
		}
		if (!u.put(parts[i])) {
			return (false, str_empty());
		}
	}
	return (true, u);
}

// Replace every occurrence of old with new.
(bool, char[..]) (Arena* a).replace(char[..] s, char[..] old, char[..] new) {
	U8 u = {0};
	size_t i = {0};
	if (a == NULL) {
		return (false, str_empty());
	}
	if (len(old) == 0) {
		return a.copy(s);
	}
	u.begin(a);
	i = 0;
	while (i < len(s)) {
		char[..] tail = s[i ..];
		auto (ok, hit) = str_find(tail, old);
		size_t off = {0};
		if (!ok) {
			if (!u.put(tail)) {
				return (false, str_empty());
			}
			return (true, u);
		}
		off = (size_t)(hit.ptr - tail.ptr);
		if (!u.put(tail[0 .. off])) {
			return (false, str_empty());
		}
		if (!u.put(new)) {
			return (false, str_empty());
		}
		i = i + off + len(old);
	}
	return (true, u);
}

// NUL-terminated copy of s in the arena (C boundary).
char* (Arena* a).z(char[..] s) {
	size_t n = {0};
	char* p = {0};
	if (a == NULL) {
		return NULL;
	}
	n = len(s);
	p = arena_bump(a, n + 1);
	if (p == NULL) {
		return NULL;
	}
	if (n != 0) {
		memcpy(p, s.ptr, n);
	}
	p[n] = '\0';
	return p;
}
