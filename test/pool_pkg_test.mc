/* Portable pool + heap T[..] builtin tests. */
#include <assert.h>
#include <stdlib.h>

import "str";

int test_copy_cat() {
	char buf[32] = { 0 };
	int rc = 0;

	pool p {
		char[..] out = { 0 };
		bool ok = p.copy(&out, "hello world");
		if (!ok) {
			rc = 1;
		} else if (cstr_write(buf, out) != 11) {
			rc = 2;
		} else if (!str_eq("hello world", ranged(buf, cstr_zlen(buf)))) {
			rc = 3;
		}
	}
	return rc;
}

int test_append() {
	char[..] s = { 0 };
	int i = { 0 };
	int rc = 0;

	pool p {
		if (!p.copy(&s, "hello")) {
			rc = 1;
		}
		if (rc == 0 && !p.append(&s, " world")) {
			rc = 2;
		}
		if (rc == 0 && !str_eq(s, "hello world")) {
			rc = 3;
		}
		if (rc == 0 && len(s) != 11) {
			rc = 4;
		}
		if (rc == 0) {
			const char[..] sub = s[0 .. 5];
			if (!str_eq(sub, "hello")) {
				rc = 5;
			} else if (!str_eq(s[6 ..], "world")) {
				rc = 6;
			}
		}
		if (rc == 0) {
			s = str_empty();
			if (len(s) != 0 || !str_eq(s, "")) {
				rc = 8;
			}
		}
		if (rc == 0) {
			char* z = p.cstr(s);

			if (z == 0 || *z != '\0') {
				rc = 9;
			}
		}
		for (; rc == 0 && i < 100; i = i + 1) {
			if (!p.push(&s, 'a')) {
				rc = 10;
			}
		}
		if (rc == 0 && len(s) != 100) {
			rc = 11;
		}
		if (rc == 0 && !str_starts_with(s, "aaa")) {
			rc = 12;
		}
	}
	return rc;
}

int test_join() {
	char buf[32] = { 0 };
	const char[..] parts[3] = { 0 };
	int rc = 0;

	parts[0] = "a";
	parts[1] = "b";
	parts[2] = "c";
	pool p {
		char[..] out = { 0 };
		bool ok = p.join(&out, ",", parts, 3);
		if (!ok) {
			rc = 1;
		} else if (!str_eq(out, "a,b,c")) {
			rc = 2;
		} else if (cstr_write(buf, out) != 5) {
			rc = 3;
		} else if (!str_eq(ranged(buf, cstr_zlen(buf)), "a,b,c")) {
			rc = 4;
		}
		if (rc == 0) {
			parts[0] = "x";
			ok = p.join(&out, ",", parts, 1);
			if (!ok) {
				rc = 8;
			} else if (!str_eq(out, "x")) {
				rc = 9;
			}
		}
		if (rc == 0) {
			parts[0] = "a";
			parts[1] = "b";
			parts[2] = "c";
			ok = p.join(&out, " | ", parts, 3);
			if (!ok) {
				rc = 5;
			} else if (!str_eq(out, "a | b | c")) {
				rc = 6;
			}
		}
		if (rc == 0) {
			ok = p.join(&out, ",", parts, 0);
			if (!ok) {
				rc = 7;
			} else if (!str_is_empty(out)) {
				rc = 10;
			}
		}
	}
	return rc;
}

int test_replace() {
	char buf[32] = { 0 };
	int rc = 0;

	pool p {
		char[..] out = { 0 };
		bool ok = p.replace(&out, "foo bar foo", "foo", "baz");
		if (!ok) {
			rc = 1;
		} else if (!str_eq(out, "baz bar baz")) {
			rc = 2;
		} else if (cstr_write(buf, out) != 11) {
			rc = 3;
		} else if (!str_eq(ranged(buf, cstr_zlen(buf)), "baz bar baz")) {
			rc = 4;
		}
		if (rc == 0) {
			ok = p.replace(&out, "x-x-x", "-", "+");
			if (!ok) {
				rc = 5;
			} else if (!str_eq(out, "x+x+x")) {
				rc = 6;
			}
		}
		if (rc == 0) {
			ok = p.replace(&out, "abc", "z", "w");
			if (!ok) {
				rc = 7;
			} else if (!str_eq(out, "abc")) {
				rc = 8;
			}
		}
	}
	return rc;
}

int test_reset() {
	int rc = 0;

	pool p {
		char[..] out = { 0 };
		bool ok = p.copy(&out, "first");
		if (!ok || !str_eq(out, "first")) {
			rc = 1;
		} else {
			p.reset();
			ok = p.copy(&out, "second");
			if (!ok || !str_eq(out, "second")) {
				rc = 2;
			}
		}
	}
	return rc;
}

int test_heap_and_helper() {
	int rc = 0;
	int[..] a = { 0 };

	if (!push(&a, 42) || len(a) != 1 || a[0] != 42) {
		rc = 1;
	}
	release(&a);
	pool p {
		char[..] s = { 0 };
		if (!p.copy(&s, "x")) {
			rc = 2;
		} else if (!p.push(&s, 'y') || !str_eq(s, "xy")) {
			rc = 3;
		}
	}
	return rc;
}

int test_typed_alloc() {
	int rc = 0;
	int[..] heap = alloc(int, 4);
	int* hp = alloc(int);

	if (len(heap) != 4 || cap(heap) != 4) {
		rc = 1;
	} else {
		heap[0] = 10;
		heap[3] = 40;
		if (heap[0] != 10 || heap[3] != 40) {
			rc = 2;
		}
	}
	release(&heap);
	if (hp == NULL) {
		if (rc == 0) {
			rc = 3;
		}
	} else if (rc == 0) {
		*hp = 7;
		if (*hp != 7) {
			rc = 4;
		}
	}
	free(hp);
	pool p {
		int[..] s = p.alloc(int, 3);
		int* q = p.alloc(int);

		if (rc == 0 && (len(s) != 3 || cap(s) != 3)) {
			rc = 5;
		} else if (rc == 0) {
			s[0] = 1;
			s[2] = 3;
			if (s[0] != 1 || s[2] != 3) {
				rc = 6;
			}
		}
		if (rc == 0) {
			if (q == NULL) {
				rc = 7;
			} else {
				*q = 9;
				if (*q != 9) {
					rc = 8;
				}
			}
		}
		if (rc == 0) {
			int[..] empty = p.alloc(int, 0);

			if (len(empty) != 0 || cap(empty) != 0) {
				rc = 9;
			}
		}
	}
	return rc;
}

int pool_pkg_run() {
	if (test_copy_cat() != 0) {
		return 1;
	}
	if (test_append() != 0) {
		return 2;
	}
	if (test_join() != 0) {
		return 3;
	}
	if (test_replace() != 0) {
		return 4;
	}
	if (test_reset() != 0) {
		return 5;
	}
	if (test_heap_and_helper() != 0) {
		return 6;
	}
	if (test_typed_alloc() != 0) {
		return 7;
	}
	return 0;
}

int main() {
	return pool_pkg_run();
}
