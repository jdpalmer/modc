#include <stddef.h>
#include <string.h>

overload size_t write_cap(char dst[], char[..] src, size_t cap) {
	size_t n = { 0 };
	n = len(src);
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

overload size_t write_buf(char[..] dst, char[..] src) {
	return write_cap(ptr(dst), src, cap(dst));
}

overload void take_int(int[..] x) {
	(void)x;
}

int test_fixed() {
	char buf[8] = { 0 };
	char[..] s = { 0 };
	s = "hi";
	if (write_buf(buf, s) != 2) {
		return 1;
	}
	if (buf[0] != 'h' || buf[1] != 'i' || buf[2] != '\0') {
		return 2;
	}
	return 0;
}

int test_literal() {
	char buf[8] = { 0 };
	if (write_buf(buf, "ab") != 2) {
		return 1;
	}
	if (strcmp(buf, "ab") != 0) {
		return 2;
	}
	return 0;
}

int test_sizing() {
	if (write_cap(NULL, "abc", 0) != 3) {
		return 1;
	}
	return 0;
}

int test_int_ranged() {
	int a[4] = { 0 };
	take_int(a);
	return 0;
}
