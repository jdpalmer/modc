/* Portable test (was range_hooks.mc + range_hooks_main.c). */
#include <assert.h>

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

overload int range_at(MyBuf b, size_t i) {
	int[..] s = ranged(b.data, b.n);

	return s[(int)i];
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
	for (auto x: b) {
		s = s + x;
	}
	return s;
}

int sum_buf_local() {
	int a[4] = { 1, 2, 3, 4 };
	MyBuf b = { a, 4 };
	return sum_buf(b);
}

int sum_triple() {
	Triple t = { 10, 20, 30 };
	int s = { 0 };
	for (auto x: t) {
		s = s + x;
	}
	return s;
}

int main() {
	assert_eq(sum_buf_local(), 10);
	assert_eq(sum_triple(), 60);
	return 0;
}
