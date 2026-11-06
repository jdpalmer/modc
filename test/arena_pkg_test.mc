/* Portable test (was arena_pkg.mc + arena_pkg_main.c). */
#include <assert.h>

import "arena";

int test_copy_cat() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	int rc = 0;

	a.init();
	auto (ok, out) = a.copy("hello world");
	if (!ok) {
		rc = 1;
	} else if (cstr_write(buf, out) != 11) {
		rc = 2;
	} else if (!str_eq("hello world", ranged(buf, cstr_zlen(buf)))) {
		rc = 3;
	}
	defer a.free();
	return rc;
}

int test_append() {
	Arena a = { 0 };
	char[..] s = { 0 };
	int i = { 0 };
	int rc = 0;

	a.init();
	{
		auto (ok, next) = a.append(str_empty(), "hello");
		if (!ok) {
			rc = 1;
		} else {
			s = next;
		}
	}
	if (rc == 0) {
		auto (ok, next) = a.append(s, " world");
		if (!ok) {
			rc = 2;
		} else {
			s = next;
		}
	}
	if (rc == 0 && !str_eq(s, "hello world")) {
		rc = 3;
	}
	if (rc == 0 && len(s) != 11) {
		rc = 4;
	}
	if (rc == 0) {
		const char[..] sub = { 0 };

		sub = s[0 .. 5];
		if (!str_eq(sub, "hello")) {
			rc = 5;
		} else {
			sub = s[6 ..];
			if (!str_eq(sub, "world")) {
				rc = 6;
			}
		}
	}
	if (rc == 0) {
		s = str_empty();
		if (len(s) != 0 || !str_eq(s, "")) {
			rc = 8;
		}
	}
	if (rc == 0) {
		char* z = a.z(s);

		if (z == 0 || *z != '\0') {
			rc = 9;
		}
	}
	for (; rc == 0 && i < 100; i = i + 1) {
		auto (ok, next) = a.append_byte(s, 'a');
		if (!ok) {
			rc = 10;
		} else {
			s = next;
		}
	}
	if (rc == 0 && len(s) != 100) {
		rc = 11;
	}
	if (rc == 0 && !str_starts_with(s, "aaa")) {
		rc = 12;
	}
	defer a.free();
	return rc;
}

int test_join() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	const char[..] parts[3] = { 0 };
	int rc = 0;

	parts[0] = "a";
	parts[1] = "b";
	parts[2] = "c";
	a.init();
	auto (ok, out) = a.join(",", parts, 3);
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
		auto (join_ok, next) = a.join(",", parts, 1);
		ok = join_ok;
		out = next;
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
		auto (join_ok, next) = a.join(" | ", parts, 3);
		ok = join_ok;
		out = next;
		if (!ok) {
			rc = 5;
		} else if (!str_eq(out, "a | b | c")) {
			rc = 6;
		}
	}
	if (rc == 0) {
		auto (join_ok, next) = a.join(",", parts, 0);
		ok = join_ok;
		out = next;
		if (!ok) {
			rc = 7;
		} else if (!str_is_empty(out)) {
			rc = 10;
		}
	}
	defer a.free();
	return rc;
}

int test_replace() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	int rc = 0;

	a.init();
	auto (ok, out) = a.replace("foo bar foo", "foo", "baz");
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
		auto (rep_ok, next) = a.replace("x-x-x", "-", "+");
		ok = rep_ok;
		out = next;
		if (!ok) {
			rc = 5;
		} else if (!str_eq(out, "x+x+x")) {
			rc = 6;
		}
	}
	if (rc == 0) {
		auto (rep_ok, next) = a.replace("abc", "z", "w");
		ok = rep_ok;
		out = next;
		if (!ok) {
			rc = 7;
		} else if (!str_eq(out, "abc")) {
			rc = 8;
		}
	}
	defer a.free();
	return rc;
}

int test_reset() {
	Arena a = { 0 };
	Arena b = { 0 };
	int rc = 0;

	a.init();
	auto (ok, out) = a.copy("first");
	if (!ok || !str_eq(out, "first")) {
		rc = 1;
	} else {
		/* reset consumes region views of a; allocate from a fresh arena. */
		a.reset();
		b.init();
		auto (copy_ok, next) = b.copy("second");
		ok = copy_ok;
		out = next;
		if (!ok || !str_eq(out, "second")) {
			rc = 2;
		}
	}
	defer a.free();
	defer b.free();
	return rc;
}

int arena_pkg_run() {
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
	return 0;
}

int main() {
	return arena_pkg_run();
}
