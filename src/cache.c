/*
 * Durable build cache: content-addressed foreign .o and per-package objects.
 *
 * Layout under .modc-cache/:
 *   foreign/<hex>.o
 *   pkg/<id>-<hex>/pkg.o + iface + needs
 *   prog/<hex>/linkmeta + stamp
 *
 * Keys include QBE target and host OS; project-local paths are relative.
 */
#include "ast.h"
#include "host_os.h"

#include <stdio.h>
#include <string.h>

enum { CacheHexLen = 16 }; /* 64-bit hex */

// FNV-1a 64-bit hash of n bytes, continuing from seed h (0 picks the offset basis).
static unsigned long long
fnv1a64(const void* data, size_t n, unsigned long long h) {
	const unsigned char* p = data;

	if (h == 0) {
		h = 14695981039346656037ull;
	}
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

// Hash an arbitrary byte buffer for cache keys.
unsigned long long
cache_hash_bytes(const void* data, size_t n) {
	return fnv1a64(data, n, 0);
}

// Hash a C string (NULL treated as empty).
unsigned long long
cache_hash_str(const char* s) {
	if (s == NULL) {
		return cache_hash_bytes("", 0);
	}
	return cache_hash_bytes(s, strlen(s));
}

// Hash file contents; returns 0 if the file cannot be read.
unsigned long long
cache_hash_file(const char* path) {
	size_t n;
	unsigned long long h;
	char* text;

	text = read_file(path, &n);
	if (text == NULL)
		return 0;
	h = cache_hash_bytes(text, n);
	free(text);
	return h;
}

// Fold hash b into a (order-sensitive mix for composing keys).
unsigned long long
cache_hash_mix(unsigned long long a, unsigned long long b) {
	return fnv1a64(&b, sizeof(b), a ? a : 14695981039346656037ull);
}

// Format h as a fixed-width lowercase hex string for cache path segments.
void
cache_hash_hex(unsigned long long h, char* out, size_t out_len) {
	if (out_len < CacheHexLen + 1) {
		if (out_len) {
			out[0] = 0;
		}
		return;
	}
	snprintf(out, out_len, "%016llx", (unsigned long long)h);
}

// Create parent directories for path (best-effort mkdir -p of dirname).
static int
mkdir_parents(const char* path) {
	char buf[HOST_PATH_MAX];
	size_t i, n;

	if (path == NULL || path[0] == 0)
		return 1;
	snprintf(buf, sizeof(buf), "%s", path);
	n = strlen(buf);
	for (i = 1; i < n; i++) {
		if (!host_path_is_sep((unsigned char)buf[i]))
			continue;
		buf[i] = 0;
		if (buf[0] && host_mkdir(buf) != 0 && !host_is_dir(buf))
			return 1;
		buf[i] = '/';
	}
	return 0;
}

// Create dir and all missing parents (mkdir -p); 0 on success.
int
cache_mkdir_p(const char* dir) {
	char path[HOST_PATH_MAX];
	size_t n;

	if (dir == NULL || dir[0] == 0)
		return 1;
	snprintf(path, sizeof(path), "%s", dir);
	n = strlen(path);
	while (n > 1 && host_path_is_sep((unsigned char)path[n - 1])) {
		path[n - 1] = 0;
		n--;
	}
	if (mkdir_parents(path) != 0)
		return 1;
	if (host_mkdir(path) != 0 && !host_is_dir(path))
		return 1;
	return 0;
}

// Resolve .modc-cache path: walk up for modc.ini, else beside entry.
int
cache_root_for(const char* entry, char* out, size_t out_len) {
	char abs[HOST_PATH_MAX], parent[HOST_PATH_MAX], probe[HOST_PATH_MAX];
	char cur[HOST_PATH_MAX];

	if (out == NULL || out_len == 0)
		return 1;
	if (entry == NULL || entry[0] == 0)
		entry = ".";
	if (host_abspath(entry, abs, sizeof(abs)) != 0)
		snprintf(abs, sizeof(abs), "%s", entry);
	if (host_is_file(abs))
		host_dirname(abs, parent, sizeof(parent));
	else
		snprintf(parent, sizeof(parent), "%s", abs);

	snprintf(cur, sizeof(cur), "%s", parent);
	for (;;) {
		snprintf(probe, sizeof(probe), "%s/modc.ini", cur);
		if (host_is_file(probe)) {
			snprintf(out, out_len, "%s/.modc-cache", cur);
			return 0;
		}
		snprintf(probe, sizeof(probe), "%s", cur);
		host_dirname(probe, parent, sizeof(parent));
		if (strcmp(parent, cur) == 0)
			break;
		snprintf(cur, sizeof(cur), "%s", parent);
	}
	/* Fall back to directory of the entry path. */
	if (host_is_file(abs))
		host_dirname(abs, parent, sizeof(parent));
	else
		snprintf(parent, sizeof(parent), "%s", abs);
	snprintf(out, out_len, "%s/.modc-cache", parent);
	return 0;
}

// Directory that owns .modc-cache (parent of cache_root_for); for relative keys.
int
cache_project_root(const char* entry, char* out, size_t out_len) {
	char crooot[HOST_PATH_MAX];

	if (out == NULL || out_len == 0)
		return 1;
	if (cache_root_for(entry, crooot, sizeof(crooot)) != 0)
		return 1;
	host_dirname(crooot, out, out_len);
	return out[0] == 0;
}

// Canonicalize path for hashing: project-relative with / if under projroot.
void
cache_path_key(const char* path, const char* projroot, char* out, size_t out_len) {
	char ap[HOST_PATH_MAX], ar[HOST_PATH_MAX];
	size_t rn;

	if (out == NULL || out_len == 0)
		return;
	out[0] = 0;
	if (path == NULL || path[0] == 0)
		return;
	if (host_abspath(path, ap, sizeof(ap)) != 0)
		snprintf(ap, sizeof(ap), "%s", path);
	if (projroot && projroot[0] && host_abspath(projroot, ar, sizeof(ar)) == 0) {
		rn = strlen(ar);
		while (rn > 1 && host_path_is_sep((unsigned char)ar[rn - 1]))
			ar[--rn] = 0;
		if (rn > 0 && strncmp(ap, ar, rn) == 0 &&
		    (ap[rn] == 0 || host_path_is_sep((unsigned char)ap[rn]))) {
			const char* rel = ap + rn;
			while (*rel && host_path_is_sep((unsigned char)*rel))
				rel++;
			snprintf(out, out_len, "%s", rel[0] ? rel : ".");
			host_path_slashify(out);
			return;
		}
	}
	snprintf(out, out_len, "%s", ap);
	host_path_slashify(out);
}

// Print "modc cache hit|miss …" when -v is set.
void
cache_log(int verbose, const char* hitmiss, const char* what) {
	if (!verbose)
		return;
	fprintf(stderr, "modc cache %s %s\n", hitmiss, what ? what : "");
}

// Copy src to dst, creating parent dirs; used to materialize cache hits.
int
cache_copy_file(const char* src, const char* dst) {
	char* text;
	size_t n;
	FILE* f;

	if (mkdir_parents(dst) != 0)
		return 1;
	text = read_file(src, &n);
	if (text == NULL)
		return 1;
	f = fopen(dst, "wb");
	if (f == NULL) {
		free(text);
		return 1;
	}
	if (n && fwrite(text, 1, n, f) != n) {
		fclose(f);
		free(text);
		return 1;
	}
	fclose(f);
	free(text);
	return 0;
}

// Write n bytes to path (creates parents); for iface/needs/stamp blobs.
int
cache_write_bytes(const char* path, const void* data, size_t n) {
	FILE* f;

	if (mkdir_parents(path) != 0)
		return 1;
	f = fopen(path, "wb");
	if (f == NULL)
		return 1;
	if (n && fwrite(data, 1, n, f) != n) {
		fclose(f);
		return 1;
	}
	fclose(f);
	return 0;
}

// Write a NUL-terminated string to path (NULL becomes empty).
int
cache_write_str(const char* path, const char* s) {
	if (s == NULL)
		s = "";
	return cache_write_bytes(path, s, strlen(s));
}

// Read a small text cache file into out, trimming trailing newlines.
int
cache_read_str(const char* path, char* out, size_t out_len) {
	char* text;
	size_t n;

	if (out == NULL || out_len == 0)
		return 1;
	out[0] = 0;
	text = read_file(path, &n);
	if (text == NULL)
		return 1;
	if (n >= out_len)
		n = out_len - 1;
	memcpy(out, text, n);
	out[n] = 0;
	/* trim trailing newlines */
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		out[--n] = 0;
	free(text);
	return 0;
}
