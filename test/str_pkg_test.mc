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
		auto (ok, hit) = str_find(hay, needle);
		if (!ok || !str_eq(hit, needle)) {
			return 1;
		}
	}
	{
		auto (ok, hit) = str_ifind(hay, "LO");
		if (!ok || !str_eq(hit, "lo")) {
			return 2;
		}
	}
	{
		auto (ok, hit) = str_rfind(hay, "l");
		if (!ok || !str_eq(hit, "l")) {
			return 3;
		}
	}
	{
		auto (ok, miss) = str_find(hay, "xyz");
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
	const char[..] tok = str_chomp(s);
	if (!str_eq(tok, "line")) {
		return 1;
	}
	return 0;
}

int test_sep_trim() {
	char line[64] = { 0 };
	int n = { 0 };
	cstr_write(line, "  foo, bar , baz  ");
	const char[..] rest = str_from_cstr(line);
	auto (tok, rest1) = str_split_once(rest, " ");
	rest = rest1;
	while (str_is_empty(tok) && !str_is_empty(rest)) {
		auto (t, r) = str_split_once(rest, " ");
		tok = t;
		rest = r;
	}
	if (!str_eq(tok, "foo,")) {
		return 1;
	}
	tok = str_trim("  hi  ");
	if (!str_eq(tok, "hi")) {
		return 2;
	}
	if (test_chomp() != 0) {
		return 3;
	}
	cstr_write(line, "a=b=c");
	rest = str_from_cstr(line);
	while (!str_is_empty(rest)) {
		auto (t, r) = str_split_once(rest, "=");
		tok = t;
		rest = r;
		n = n + 1;
		if (n == 1 && !str_eq(tok, "a")) {
			return 4;
		}
		if (n == 2 && !str_eq(tok, "b")) {
			return 5;
		}
		if (n == 3 && !str_eq(tok, "c")) {
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
	if (!str_starts_with(s, "He")) {
		return 1;
	}
	if (!str_ends_with(s, "lo")) {
		return 2;
	}
	if (str_cmp(s, "Hello") != 0) {
		return 3;
	}
	if (str_icmp(s, "hello") != 0) {
		return 4;
	}
	return 0;
}

int test_parse() {
	{
		auto (ok, v) = str_to_long("12345", 10);
		if (!ok || v != 12345) {
			return 1;
		}
	}
	{
		auto (ok, v) = str_to_long("-99", 10);
		if (!ok || v != -99) {
			return 2;
		}
	}
	{
		auto (ok, badv) = str_to_long("gg", 10);
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
	if (!str_eq(word, "hi")) {
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
	if (!str_eq(s, "abcd") || cap(s) < 4) {
		str_free(&s);
		return 3;
	}
	if (!str_append_byte(&s, '!')) {
		str_free(&s);
		return 4;
	}
	if (!str_eq(s, "abcd!")) {
		str_free(&s);
		return 5;
	}
	if (!str_set(&s, "x")) {
		str_free(&s);
		return 6;
	}
	if (!str_eq(s, "x") || !str_reserve(&s, 32) || cap(s) < 32) {
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
	if (!str_set(&t, "hi") || !str_set(&t, t[1 .. 2]) || !str_eq(t, "i")) {
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
