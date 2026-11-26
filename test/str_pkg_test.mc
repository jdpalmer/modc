/* Portable test (was str_pkg.mc + str_pkg_main.c). */
#include <assert.h>

import "str";

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int test_cstr_write() {
	char buf[32] = { 0 };
	const char[..] a = "hello";
	if (cstr_write(buf, a) != 5) {
		return 1;
	}
	if (!str_eq_cstr(a, buf)) {
		return 2;
	}
	return 0;
}

int test_find() {
	const char[..] hay = "hello world";
	const char[..] needle = "world";
	{
		auto (ok, hit) = hay.find(needle);
		if (!ok || !hit.eq(needle)) {
			return 1;
		}
	}
	{
		auto (ok, hit) = hay.ifind("LO");
		if (!ok || !hit.eq("lo")) {
			return 2;
		}
	}
	{
		auto (ok, hit) = hay.rfind("l");
		if (!ok || !hit.eq("l")) {
			return 3;
		}
	}
	{
		auto (ok, miss) = hay.find("xyz");
		(void)miss;
		if (ok) {
			return 4;
		}
	}
	return 0;
}

int test_chomp() {
	char raw[8] = { 0 };
	raw[0] = 'l';
	raw[1] = 'i';
	raw[2] = 'n';
	raw[3] = 'e';
	raw[4] = '\n';
	raw[5] = 0;
	const char[..] s = ranged(raw, 5);
	const char[..] tok = s.chomp();
	if (!tok.eq("line")) {
		return 1;
	}
	return 0;
}

int test_sep_trim() {
	char line[64] = { 0 };
	int n = { 0 };
	cstr_write(line, "  foo, bar , baz  ");
	const char[..] rest = str_from_cstr(line);
	auto (tok, rest1) = rest.split_once(" ");
	rest = rest1;
	while (tok.is_empty() && !rest.is_empty()) {
		auto (t, r) = rest.split_once(" ");
		tok = t;
		rest = r;
	}
	if (!tok.eq("foo,")) {
		return 1;
	}
	tok = "  hi  ".trim();
	if (!tok.eq("hi")) {
		return 2;
	}
	if (test_chomp() != 0) {
		return 3;
	}
	cstr_write(line, "a=b=c");
	rest = str_from_cstr(line);
	while (!rest.is_empty()) {
		auto (t, r) = rest.split_once("=");
		tok = t;
		rest = r;
		n = n + 1;
		if (n == 1 && !tok.eq("a")) {
			return 4;
		}
		if (n == 2 && !tok.eq("b")) {
			return 5;
		}
		if (n == 3 && !tok.eq("c")) {
			return 6;
		}
	}
	if (n != 3) {
		return 7;
	}
	return 0;
}

int test_cmp_prefix() {
	const char[..] s = "Hello";
	if (!s.starts_with("He")) {
		return 1;
	}
	if (!s.ends_with("lo")) {
		return 2;
	}
	if (s.cmp("Hello") != 0) {
		return 3;
	}
	if (s.icmp("hello") != 0) {
		return 4;
	}
	return 0;
}

int test_parse() {
	{
		auto (ok, v) = "12345".to_long(10);
		if (!ok || v != 12345) {
			return 1;
		}
	}
	{
		auto (ok, v) = "-99".to_long(10);
		if (!ok || v != -99) {
			return 2;
		}
	}
	{
		auto (ok, badv) = "gg".to_long(10);
		(void)badv;
		if (ok) {
			return 3;
		}
	}
	return 0;
}

int test_subview() {
	char data[16] = { 0 };
	data[0] = 'h';
	data[1] = 'i';
	data[2] = '!';
	char[..] chunk = ranged(data, 3);
	char[..] word = chunk[0 .. 2];
	if (!word.eq("hi")) {
		return 1;
	}
	return 0;
}

int test_heap() {
	char[..] t = { 0 };
	auto (ok0, s) = str_dup("ab");
	if (!ok0 || len(s) != 2 || s[0] != 'a') {
		str_free(&s);
		return 1;
	}
	if (!str_append(&s, "cd")) {
		str_free(&s);
		return 2;
	}
	if (!s.eq("abcd") || cap(s) < 4) {
		str_free(&s);
		return 3;
	}
	if (!str_append_byte(&s, '!')) {
		str_free(&s);
		return 4;
	}
	if (!s.eq("abcd!")) {
		str_free(&s);
		return 5;
	}
	if (!str_set(&s, "x")) {
		str_free(&s);
		return 6;
	}
	if (!s.eq("x") || !str_reserve(&s, 32) || cap(s) < 32) {
		str_free(&s);
		return 7;
	}
	{
		char* z = cstr(s);

		if (z == NULL || strcmp(z, "x") != 0 || len(s) != 1) {
			free(z);
			str_free(&s);
			return 8;
		}
		free(z);
	}
	str_free(&s);
	{
		auto (ok, n) = str_dup(str_empty());
		if (!ok || (void*)n != NULL) {
			str_free(&n);
			return 10;
		}
		str_free(&n);
	}
	if (!str_set(&t, "hi") || !str_set(&t, t[1 .. 2]) || !t.eq("i")) {
		str_free(&t);
		return 11;
	}
	str_free(&t);
	str_free(NULL);
	return 0;
}

int str_pkg_run() {
	if (test_cstr_write() != 0) {
		return 1;
	}
	if (test_find() != 0) {
		return 2;
	}
	if (test_sep_trim() != 0) {
		return 3;
	}
	if (test_cmp_prefix() != 0) {
		return 4;
	}
	if (test_parse() != 0) {
		return 5;
	}
	if (test_subview() != 0) {
		return 6;
	}
	if (test_heap() != 0) {
		return 7;
	}
	return 0;
}

int main() {
	return str_pkg_run();
}
