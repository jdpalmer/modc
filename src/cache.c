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
static uint64_t
fnv1a64(const void* data, size_t n, uint64_t h) {
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
uint64_t
cache_hash_bytes(const void* data, size_t n) {
	return fnv1a64(data, n, 0);
}

// Hash a C string (NULL treated as empty).
uint64_t
cache_hash_str(const char* s) {
	if (s == NULL) {
		return cache_hash_bytes("", 0);
	}
	return cache_hash_bytes(s, strlen(s));
}

// Hash file contents; returns 0 if the file cannot be read.
uint64_t
cache_hash_file(const char* path) {
	size_t n;
	uint64_t h;
	char* text;

	text = read_file(path, &n);
	if (text == NULL)
		return 0;
	h = cache_hash_bytes(text, n);
	free(text);
	return h;
}

// Fold hash b into a (order-sensitive mix for composing keys).
uint64_t
cache_hash_mix(uint64_t a, uint64_t b) {
	return fnv1a64(&b, sizeof(b), a ? a : 14695981039346656037ull);
}

// Format h as a fixed-width lowercase hex string for cache path segments.
void
cache_hash_hex(uint64_t h, char* out, size_t out_len) {
	if (out_len < CacheHexLen + 1) {
		if (out_len) {
			out[0] = 0;
		}
		return;
	}
	snprintf(out, out_len, "%016" PRIx64, (uint64_t)h);
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
		host_unlink(dst);
		free(text);
		return 1;
	}
	if (fclose(f) != 0) {
		host_unlink(dst);
		free(text);
		return 1;
	}
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
		host_unlink(path);
		return 1;
	}
	if (fclose(f) != 0) {
		host_unlink(path);
		return 1;
	}
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

// Persist dependency paths with their current content hashes.
int
cache_write_deps(const char* path, char** files, int nfiles) {
	char *buf, line[32];
	size_t len, cap, n;
	uint64_t h;
	int i, j;

	len = 0;
	cap = 256;
	buf = xmalloc(cap);
	for (i = 0; i < nfiles; i++) {
		if (files[i] == NULL || files[i][0] == 0)
			continue;
		if (strchr(files[i], '\n') || strchr(files[i], '\r') || strchr(files[i], '\t')) {
			free(buf);
			return 1;
		}
		for (j = 0; j < i; j++)
			if (files[j] && strcmp(files[j], files[i]) == 0)
				break;
		if (j < i)
			continue;
		h = cache_hash_file(files[i]);
		if (h == 0) {
			free(buf);
			return 1;
		}
		snprintf(line, sizeof(line), "%016" PRIx64 "\t", h);
		n = strlen(line) + strlen(files[i]) + 1;
		while (len + n > cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		memcpy(buf + len, line, strlen(line));
		len += strlen(line);
		memcpy(buf + len, files[i], strlen(files[i]));
		len += strlen(files[i]);
		buf[len++] = '\n';
	}
	i = cache_write_bytes(path, buf, len);
	free(buf);
	return i;
}

// Validate every dependency recorded by cache_write_deps.
int
cache_deps_valid(const char* path) {
	char *text, *p, *line, hex[17], *end;
	uint64_t want;

	text = read_file(path, NULL);
	if (text == NULL)
		return 0;
	for (p = text; *p;) {
		line = p;
		p = strchr(p, '\n');
		if (p)
			*p++ = 0;
		else
			p = line + strlen(line);
		if (strlen(line) < 18 || line[16] != '\t') {
			free(text);
			return 0;
		}
		memcpy(hex, line, 16);
		hex[16] = 0;
		errno = 0;
		want = strtoull(hex, &end, 16);
		if (errno || *end || cache_hash_file(line + 17) != want) {
			free(text);
			return 0;
		}
	}
	free(text);
	return 1;
}

// Convert a GCC/Clang make-style depfile into content-hashed dependencies.
int
cache_depfile_to_deps(const char* depfile, const char* path) {
	char *text, *p, *q, *tok;
	char** files;
	size_t n;
	int nfiles, cap, r;

	text = read_file(depfile, &n);
	if (text == NULL)
		return 1;
	p = text;
	while (*p && !(*p == ':' && isspace((unsigned char)p[1])))
		p++;
	if (*p == 0) {
		free(text);
		return 1;
	}
	p++;
	nfiles = 0;
	cap = 8;
	files = xmalloc((size_t)cap * sizeof(char*));
	while (*p) {
		while (isspace((unsigned char)*p) ||
		       (*p == '\\' && (p[1] == '\n' ||
					(p[1] == '\r' && p[2] == '\n')))) {
			if (*p == '\\') {
				p += p[1] == '\r' ? 3 : 2;
			} else
				p++;
		}
		if (*p == 0)
			break;
		tok = xmalloc(strlen(p) + 1);
		q = tok;
		while (*p && !isspace((unsigned char)*p)) {
			if (*p == '\\' && p[1]) {
				if (p[1] == '\n') {
					p += 2;
					break;
				}
				if (p[1] == '\r' && p[2] == '\n') {
					p += 3;
					break;
				}
				p++;
			}
			if (*p == '$' && p[1] == '$')
				p++;
			*q++ = *p++;
		}
		*q = 0;
		if (tok[0]) {
			if (nfiles >= cap) {
				cap *= 2;
				files = xrealloc(files, (size_t)cap * sizeof(char*));
			}
			files[nfiles++] = tok;
		} else
			free(tok);
	}
	r = cache_write_deps(path, files, nfiles);
	while (nfiles > 0)
		free(files[--nfiles]);
	free(files);
	free(text);
	return r;
}
