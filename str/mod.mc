// str — length-bounded char[..] views (non-owning) and cstr_* C boundary helpers.
//
// char[..] views do not own storage. Use import "arena" for Arena building in a region.
//
// Preconditions (caller bugs are not swallowed):
//   char dst[] + cap — when cap > 0, dst is writable; cap == 0 or dst == NULL is
//     sizing-only on cstr_write (strlcpy-style): no write, return required length.
//
// Semantic NULL (documented, not bugs):
//   str_from_cstr(NULL) and str_eq_cstr(a, NULL) treat NULL C strings as empty.
//
// API: char[..] / str_* — read, slice, compare, split, trim; *_cstr reads char*.
//      Prefer char[..] / non-*_cstr for string literals (str_eq(s, "x"));
//      str_from_cstr / *_cstr are for foreign NUL-terminated char*.
//      cstr_* — write into caller char[N] (NUL-terminated C boundary).
//
// Export: cstr_write(buf, view) on fixed char[N]; cstr_zlen(buf) / cstr_reset(buf) same.
//   Three-arg cstr_* forms for sizing-only (cap==0 / dst==NULL) and open arrays.
//
// str_split_once(s, delims) → (token, rest); no delimiter → (s, empty).
// str_find / str_ifind / str_rfind / str_irfind: empty needle → (true, hay[0..0]) or
//   hay[hlen..hlen] for reverse on non-empty hay.
// str_icmp / str_ifind / str_irfind: ASCII case fold only (not Unicode).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Lowercase an ASCII byte; other bytes unchanged.
static char str_byte_tolower(char c) {
	if (c >= 'A' && c <= 'Z') {
		return (char)(c + ('a' - 'A'));
	}
	return c;
}

// True when c appears in the delimiter/view set.
static bool str_byte_in(char[..] set, char c) {
	for (size_t i = 0; i < len(set); i++) {
		if (set[i] == c) {
			return true;
		}
	}
	return false;
}

// True for ASCII whitespace bytes.
static bool str_is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Empty view (zero length; ptr is NULL).
char[..] str_empty() {
	return ranged((char*)0, 0);
}

// View over [p, p+n); p may be NULL only when n is 0.
char[..] str_from_bytes(char* p, size_t n) {
	return ranged(p, n);
}

// View over a NUL-terminated C string; NULL yields empty.
char[..] str_from_cstr(char* p) {
	if (p == NULL) {
		return str_empty();
	}
	return ranged(p, strlen(p));
}

// True when the view has length zero.
bool str_is_empty(char[..] s) {
	return len(s) == 0;
}

// Byte-wise equality of two views.
bool str_eq(char[..] a, char[..] b) {
	size_t n = len(a);
	if (n != len(b)) {
		return false;
	}
	return memcmp(ptr(a), ptr(b), n) == 0;
}

// Equality against a NUL-terminated C string.
bool str_eq_cstr(char[..] a, char* z) {
	if (z == NULL) {
		return len(a) == 0;
	}
	size_t n = strlen(z);
	if (n != len(a)) {
		return false;
	}
	return memcmp(ptr(a), z, n) == 0;
}

// memcmp-style ordering; shorter view sorts first when prefixes match.
int str_cmp(char[..] a, char[..] b) {
	size_t n = len(a);
	if (len(b) < n) {
		n = len(b);
	}
	if (n != 0) {
		int r = memcmp(ptr(a), ptr(b), n);
		if (r != 0) {
			return r;
		}
	}
	if (len(a) < len(b)) {
		return -1;
	}
	if (len(a) > len(b)) {
		return 1;
	}
	return 0;
}

// Case-insensitive ASCII byte compare; shorter view sorts first when prefixes match.
int str_icmp(char[..] a, char[..] b) {
	size_t n = len(a);
	if (len(b) < n) {
		n = len(b);
	}
	for (size_t i = 0; i < n; i++) {
		char ca = str_byte_tolower(a[i]);
		char cb = str_byte_tolower(b[i]);
		if (ca != cb) {
			return (int)ca - (int)cb;
		}
	}
	if (len(a) < len(b)) {
		return -1;
	}
	if (len(a) > len(b)) {
		return 1;
	}
	return 0;
}

// True when s begins with prefix.
bool str_starts_with(char[..] s, char[..] prefix) {
	size_t n = len(prefix);
	if (n > len(s)) {
		return false;
	}
	return memcmp(ptr(s), ptr(prefix), n) == 0;
}

// str_starts_with against a C string.
bool str_starts_with_cstr(char[..] s, char* prefix) {
	return str_starts_with(s, str_from_cstr(prefix));
}

// True when s ends with suffix.
bool str_ends_with(char[..] s, char[..] suffix) {
	size_t n = len(suffix);
	if (n > len(s)) {
		return false;
	}
	return memcmp(ptr(s) + (len(s) - n), ptr(suffix), n) == 0;
}

// First occurrence of needle in hay; (false, empty) when missing.
(bool, char[..]) str_find(char[..] hay, char[..] needle) {
	size_t hlen = len(hay);
	size_t nlen = len(needle);
	if (nlen == 0) {
		return (true, hay[0 .. 0]);
	}
	if (nlen > hlen) {
		return (false, str_empty());
	}
	for (size_t i = 0; i + nlen <= hlen; i++) {
		if (memcmp(ptr(hay) + i, ptr(needle), nlen) == 0) {
			return (true, hay[i .. i + nlen]);
		}
	}
	return (false, str_empty());
}

// Last occurrence of needle in hay.
(bool, char[..]) str_rfind(char[..] hay, char[..] needle) {
	size_t hlen = len(hay);
	size_t nlen = len(needle);
	if (nlen == 0) {
		if (hlen == 0) {
			return (true, hay[0 .. 0]);
		}
		return (true, hay[hlen .. hlen]);
	}
	if (nlen > hlen) {
		return (false, str_empty());
	}
	for (size_t i = hlen - nlen;; i--) {
		if (memcmp(ptr(hay) + i, ptr(needle), nlen) == 0) {
			return (true, hay[i .. i + nlen]);
		}
		if (i == 0) {
			break;
		}
	}
	return (false, str_empty());
}

// Case-insensitive forward search.
(bool, char[..]) str_ifind(char[..] hay, char[..] needle) {
	size_t hlen = len(hay);
	size_t nlen = len(needle);
	if (nlen == 0) {
		return (true, hay[0 .. 0]);
	}
	if (nlen > hlen) {
		return (false, str_empty());
	}
	for (size_t i = 0; i + nlen <= hlen; i++) {
		bool match = true;
		for (size_t j = 0; j < nlen; j++) {
			char hc = str_byte_tolower(hay[i + j]);
			char nc = str_byte_tolower(needle[j]);
			if (hc != nc) {
				match = false;
				break;
			}
		}
		if (match) {
			return (true, hay[i .. i + nlen]);
		}
	}
	return (false, str_empty());
}

// Case-insensitive reverse search.
(bool, char[..]) str_irfind(char[..] hay, char[..] needle) {
	size_t hlen = len(hay);
	size_t nlen = len(needle);
	if (nlen == 0) {
		if (hlen == 0) {
			return (true, hay[0 .. 0]);
		}
		return (true, hay[hlen .. hlen]);
	}
	if (nlen > hlen) {
		return (false, str_empty());
	}
	for (size_t i = hlen - nlen;; i--) {
		bool match = true;
		for (size_t j = 0; j < nlen; j++) {
			char hc = str_byte_tolower(hay[i + j]);
			char nc = str_byte_tolower(needle[j]);
			if (hc != nc) {
				match = false;
				break;
			}
		}
		if (match) {
			return (true, hay[i .. i + nlen]);
		}
		if (i == 0) {
			break;
		}
	}
	return (false, str_empty());
}

// Empty dst (NUL at index 0). Requires writable dst when cap > 0.
overload void cstr_reset(char dst[], size_t cap) {
	if (cap == 0) {
		return;
	}
	dst[0] = '\0';
}

// cstr_reset on fixed char[N].
overload void cstr_reset(char[..] dst) {
	cstr_reset(ptr(dst), cap(dst));
}

// Length of the filled prefix before the first NUL (capped by cap).
overload size_t cstr_zlen(char dst[], size_t cap) {
	size_t i = 0;
	if (cap == 0) {
		return 0;
	}
	while (i < cap && dst[i] != '\0') {
		i = i + 1;
	}
	return i;
}

// cstr_zlen on fixed char[N].
overload size_t cstr_zlen(char[..] dst) {
	return cstr_zlen(ptr(dst), cap(dst));
}

// Write view into dst (strlcpy-style). Always NUL-terminates when cap > 0.
// cap == 0 or dst == NULL: sizing-only (no write). Returns len(src).
overload size_t cstr_write(char dst[], char[..] src, size_t cap) {
	size_t n = len(src);
	if (cap == 0 || dst == NULL) {
		return n;
	}
	if (n >= cap) {
		memcpy(dst, ptr(src), cap - 1);
		dst[cap - 1] = '\0';
		return n;
	}
	if (n != 0) {
		memcpy(dst, ptr(src), n);
	}
	dst[n] = '\0';
	return n;
}

// Write view into fixed char[N]; room is cap(dst).
overload size_t cstr_write(char[..] dst, char[..] src) {
	return cstr_write(ptr(dst), src, cap(dst));
}

// Split at the first byte in delims; no delimiter → (s, empty).
(char[..], char[..]) str_split_once(char[..] s, char[..] delims) {
	for (size_t i = 0; i < len(s); i++) {
		if (str_byte_in(delims, s[i])) {
			return (s[0 .. i], s[(i + 1) ..]);
		}
	}
	return (s, str_empty());
}

// Trim leading and trailing bytes found in chars.
char[..] str_trim_set(char[..] s, char[..] chars) {
	size_t lo = 0;
	while (lo < len(s) && str_byte_in(chars, s[lo])) {
		lo = lo + 1;
	}
	if (lo >= len(s)) {
		return str_empty();
	}
	{
		size_t hi = len(s);
		while (hi > lo && str_byte_in(chars, s[hi - 1])) {
			hi = hi - 1;
		}
		return s[lo .. hi];
	}
}

// Trim leading ASCII whitespace.
char[..] str_ltrim(char[..] s) {
	size_t i = 0;
	while (i < len(s) && str_is_space(s[i])) {
		i = i + 1;
	}
	return s[i ..];
}

// Trim trailing ASCII whitespace.
char[..] str_rtrim(char[..] s) {
	size_t n = len(s);
	while (n != 0 && str_is_space(s[n - 1])) {
		n = n - 1;
	}
	return s[0 .. n];
}

// Trim leading and trailing ASCII whitespace.
char[..] str_trim(char[..] s) {
	return str_rtrim(str_ltrim(s));
}

// Strip one trailing LF or CRLF.
char[..] str_chomp(char[..] s) {
	size_t n = len(s);
	if (n != 0 && s[n - 1] == '\n') {
		n = n - 1;
	}
	if (n != 0 && s[n - 1] == '\r') {
		n = n - 1;
	}
	return s[0 .. n];
}

// Parse base-N integer; optional leading + or -.
(bool, int64_t) str_to_long(char[..] s, int base) {
	if (base < 2 || base > 36 || len(s) == 0) {
		return (false, 0);
	}
	size_t i = 0;
	int neg = 0;
	if (s[0] == '-') {
		neg = 1;
		i = 1;
	} else if (s[0] == '+') {
		i = 1;
	}
	if (i >= len(s)) {
		return (false, 0);
	}
	int64_t v = 0;
	int64_t maxv = 9223372036854775807L / (int64_t)base;
	for (; i < len(s); i++) {
		char c = s[i];
		int digit = 0;
		if (c >= '0' && c <= '9') {
			digit = c - '0';
		} else if (c >= 'a' && c <= 'z') {
			digit = 10 + c - 'a';
		} else if (c >= 'A' && c <= 'Z') {
			digit = 10 + c - 'A';
		} else {
			return (false, 0);
		}
		if (digit >= base) {
			return (false, 0);
		}
		if (v > maxv) {
			return (false, 0);
		}
		v = v * (int64_t)base + (int64_t)digit;
	}
	if (neg) {
		v = -v;
	}
	return (true, v);
}
