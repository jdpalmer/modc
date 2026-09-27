import "path";
import "str";
#include <string.h>

int expect_z(char[..] dst, const char *want) {
	return str_eq_cstr(dst, want);
}

int test_abs_dir_base() {
	const char[..] p = { 0 };
	if (path_is_abs("a") || path_is_abs("") || path_is_abs("C:/foo")) {
		return 1;
	}
	if (!path_is_abs("/a") || !path_is_abs("/")) {
		return 2;
	}
	p = "a/b";
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
	char buf[64] = { 0 };
	char[..] empty = { 0 };
	char sentinel = 0;
	{
		auto (view, ok) = path_join(buf, "a", "b");
		if (!ok || !expect_z(view, "a/b")) {
			return 1;
		}
	}
	{
		auto (view, ok) = path_join(buf, "a/", "/b");
		if (!ok || !expect_z(view, "a/b")) {
			return 2;
		}
	}
	{
		auto (view, ok) = path_join(buf, "a", "/b");
		if (!ok || !expect_z(view, "a/b")) {
			return 3;
		}
	}
	{
		auto (view, ok) = path_join(buf, "", "/b");
		if (!ok || !expect_z(view, "/b")) {
			return 4;
		}
	}
	{
		auto (view, ok) = path_join(buf, "/", "b");
		if (!ok || !expect_z(view, "/b")) {
			return 5;
		}
	}
	{
		auto (view, ok) = path_join(buf, "/", "");
		if (!ok || !expect_z(view, "/")) {
			return 6;
		}
	}
	{
		auto (view, ok) = path_join(buf, "", "");
		if (!ok || !expect_z(view, "")) {
			return 7;
		}
	}
	sentinel = 'Z';
	empty = ranged(&sentinel, 0);
	{
		auto (view, ok) = path_join(empty, "a", "b");
		(void)view;
		if (ok || sentinel != 'Z') {
			return 8;
		}
	}
	{
		char tiny[4] = { 0 };
		tiny[0] = 'x';
		tiny[1] = 'y';
		{
			auto (view, ok) = path_join(tiny, "abcd", "e");
			(void)view;
			if (ok || tiny[0] != 0) {
				return 9;
			}
		}
	}
	return 0;
}

int test_clean() {
	char buf[64] = { 0 };
	{
		auto (view, ok) = path_clean(buf, "a//b");
		if (!ok || !expect_z(view, "a/b")) {
			return 1;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "./a");
		if (!ok || !expect_z(view, "a")) {
			return 2;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "a/./b");
		if (!ok || !expect_z(view, "a/b")) {
			return 3;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "a/../b");
		if (!ok || !expect_z(view, "b")) {
			return 4;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "/a/../b");
		if (!ok || !expect_z(view, "/b")) {
			return 5;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "a/b/..");
		if (!ok || !expect_z(view, "a")) {
			return 6;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "../a");
		if (!ok || !expect_z(view, "a")) {
			return 7;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "/../a");
		if (!ok || !expect_z(view, "/a")) {
			return 8;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "a/../../b");
		if (!ok || !expect_z(view, "b")) {
			return 9;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "/");
		if (!ok || !expect_z(view, "/")) {
			return 10;
		}
	}
	{
		auto (view, ok) = path_clean(buf, ".");
		if (!ok || !expect_z(view, "")) {
			return 11;
		}
	}
	{
		auto (view, ok) = path_clean(buf, "..");
		if (!ok || !expect_z(view, "")) {
			return 12;
		}
	}
	{
		char tiny[3] = { 0 };
		tiny[0] = 'x';
		{
			auto (view, ok) = path_clean(tiny, "foo/bar");
			(void)view;
			if (ok || tiny[0] != 0) {
				return 13;
			}
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
