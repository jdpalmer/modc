/* char[..] / C-string: query intercept + cstr; no silent decay to char*. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

void take_ranged(const char[..] x) {
	(void)x;
}

int strlen_local() {
	const char[..] name = "James";
	int n = (int)strlen(name);

	if (n != 5) {
		return 0;
	}
	return 1;
}

int strlen_temp() {
	return (int)strlen("Isaac");
}

int strcmp_slices() {
	const char[..] a = "ab";
	const char[..] b = "ab";
	const char[..] c = "ac";

	if (strcmp(a, b) != 0) {
		return 0;
	}
	if (strcmp(a, c) >= 0) {
		return 0;
	}
	return 1;
}

int pass_ranged() {
	const char[..] name = "James";

	take_ranged(name);
	take_ranged("Isaac");
	return 1;
}

int mutable_ok() {
	char buf[8] = { 0 };

	buf[0] = 'a';
	buf[1] = 'b';
	buf[2] = 'c';
	buf[3] = 0;
	{
		char[..] view = buf;

		view[0] = 'x';
		if (buf[0] != 'x') {
			return 0;
		}
	}
	return 1;
}

int cstr_literal() {
	char* p = cstr("hi");

	if (p == NULL || strcmp(p, "hi") != 0) {
		return 0;
	}
	return 1;
}

int main() {
	assert_eq(strlen_local(), 1);
	assert_eq(strlen_temp(), 5);
	assert_eq(strcmp_slices(), 1);
	assert_eq(pass_ranged(), 1);
	assert_eq(mutable_ok(), 1);
	assert_eq(cstr_literal(), 1);
	return 0;
}
