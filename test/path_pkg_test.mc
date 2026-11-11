/* Portable test (was path_pkg.mc + path_pkg_main.c). */
#include <assert.h>

import "path";
import "str";
#include <string.h>

int expect_z(char[..] dst, const char *want) {
	return str_eq_cstr(dst, want);
}

int test_abs_dir_base() {
	if (path_is_abs("a") || path_is_abs("") || path_is_abs("C:/foo")) {
		return 1;
	}
	if (!path_is_abs("/a") || !path_is_abs("/")) {
		return 2;
	}
	const char[..] p = "a/b";
	if (!str_is_empty(path_dir("a")) || !str_eq(path_base(p), "b") || !str_eq(path_dir(p), "a")) {
		return 3;
	}
	p = "/a/b";
	if (!str_eq(path_dir(p), "/a") || !str_eq(path_base(p), "b")) {
		return 4;
	}
	p = "a/b/";
	if (!str_eq(path_base(p), "b") || !str_eq(path_dir(p), "a")) {
		return 5;
	}
	p = "/";
	if (!str_eq(path_dir(p), "/") || !str_eq(path_base(p), "/")) {
		return 6;
	}
	p = "///";
	if (!str_eq(path_base(p), "/") || !str_eq(path_dir(p), "/")) {
		return 7;
	}
	return 0;
}

int test_join() {
	int rc = 0;

	pool p {
		{
			auto (view, ok) = path_join(&p, "a", "b");
			if (!ok || !expect_z(view, "a/b")) {
				rc = 1;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "a/", "/b");
			if (!ok || !expect_z(view, "a/b")) {
				rc = 2;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "a", "/b");
			if (!ok || !expect_z(view, "a/b")) {
				rc = 3;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "", "/b");
			if (!ok || !expect_z(view, "/b")) {
				rc = 4;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "/", "b");
			if (!ok || !expect_z(view, "/b")) {
				rc = 5;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "/", "");
			if (!ok || !expect_z(view, "/")) {
				rc = 6;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_join(&p, "", "");
			if (!ok || !expect_z(view, "")) {
				rc = 7;
			}
		}
	}
	if (rc != 0) {
		return rc;
	}
	{
		auto (view, ok) = path_join(0, "a", "b");
		(void)view;
		if (ok) {
			return 8;
		}
	}
	return 0;
}

int test_clean() {
	int rc = 0;

	pool p {
		{
			auto (view, ok) = path_clean(&p, "a//b");
			if (!ok || !expect_z(view, "a/b")) {
				rc = 1;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "./a");
			if (!ok || !expect_z(view, "a")) {
				rc = 2;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "a/./b");
			if (!ok || !expect_z(view, "a/b")) {
				rc = 3;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "a/../b");
			if (!ok || !expect_z(view, "b")) {
				rc = 4;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "/a/../b");
			if (!ok || !expect_z(view, "/b")) {
				rc = 5;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "a/b/..");
			if (!ok || !expect_z(view, "a")) {
				rc = 6;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "../a");
			if (!ok || !expect_z(view, "a")) {
				rc = 7;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "/../a");
			if (!ok || !expect_z(view, "/a")) {
				rc = 8;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "a/../../b");
			if (!ok || !expect_z(view, "b")) {
				rc = 9;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "/");
			if (!ok || !expect_z(view, "/")) {
				rc = 10;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, ".");
			if (!ok || !expect_z(view, "")) {
				rc = 11;
			}
		}
		if (rc == 0) {
			auto (view, ok) = path_clean(&p, "..");
			if (!ok || !expect_z(view, "")) {
				rc = 12;
			}
		}
	}
	if (rc != 0) {
		return rc;
	}
	{
		auto (view, ok) = path_clean(0, "foo/bar");
		(void)view;
		if (ok) {
			return 13;
		}
	}
	return 0;
}

int test_sys() {
	char buf[64] = { 0 };
#ifdef _WIN32
	{
		auto (view, ok) = path_to_sys(buf, "a/b");
		if (!ok || !expect_z(view, "a\\b")) {
			return 1;
		}
	}
	{
		auto (view, ok) = path_from_sys(buf, "a\\b");
		if (!ok || !expect_z(view, "a/b")) {
			return 2;
		}
	}
	{
		auto (view, ok) = path_from_sys(buf, "C:\\foo\\bar");
		if (!ok || !expect_z(view, "/C:/foo/bar") || !path_is_abs(view)) {
			return 3;
		}
	}
	{
		auto (view, ok) = path_to_sys(buf, "/C:/foo/bar");
		if (!ok || !expect_z(view, "C:\\foo\\bar")) {
			return 4;
		}
	}
#else
	{
		auto (view, ok) = path_to_sys(buf, "a/b");
		if (!ok || !expect_z(view, "a/b")) {
			return 1;
		}
	}
	{
		auto (view, ok) = path_from_sys(buf, "a/b");
		if (!ok || !expect_z(view, "a/b")) {
			return 2;
		}
	}
#endif
	{
		char tiny[1] = { 0 };
		tiny[0] = 'x';
		{
			auto (view, ok) = path_to_sys(tiny, "ab");
			(void)view;
			if (ok || tiny[0] != 0) {
				return 5;
			}
		}
	}
	return 0;
}

int path_pkg_run() {
	if (test_abs_dir_base() != 0) {
		return 1;
	}
	if (test_join() != 0) {
		return 2;
	}
	if (test_clean() != 0) {
		return 3;
	}
	if (test_sys() != 0) {
		return 4;
	}
	return 0;
}

int main() {
	return path_pkg_run();
}
