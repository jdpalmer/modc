/* range-for hooks: range_count / range_at overloads for custom types. */

#include <stddef.h>

typedef struct MyBuf MyBuf;
typedef struct Triple Triple;

struct MyBuf {
	int* data;
	int n;
};

struct Triple {
	int a;
	int b;
	int c;
};

overload size_t range_count(MyBuf b, int** out_ptr) {
	*out_ptr = b.data;
	return b.n;
}

overload size_t range_count(Triple t, int** out_ptr) {
	(void)t;
	*out_ptr = 0;
	return 3;
}

overload int range_at(Triple t, size_t i) {
	if (i == 0) {
		return t.a;
	}
	if (i == 1) {
		return t.b;
	}
	return t.c;
}

int sum_buf(MyBuf b) {
	int s = { 0 };
	s = 0;
	for (auto x: b) {
		s = s + x;
	}
	return s;
}

int sum_buf_local() {
	int a[4] = { 0 };
	MyBuf b = { 0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	b.data = a;
	b.n = 4;
	return sum_buf(b);
}

int sum_triple() {
	Triple t = { 0 };
	int s = { 0 };
	t.a = 10;
	t.b = 20;
	t.c = 30;
	s = 0;
	for (auto x: t) {
		s = s + x;
	}
	return s;
}
