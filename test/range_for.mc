#include <stdint.h>

/* range-for: fixed T[N], ranged T[..], auto / explicit type. */

int sum_array(void) {
	int a[4] = {0};
	int i = {0};
	int t = {0};
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	t = 0;
	for (i = 0; i < 4; i++) {
		a[i] = a[i];
	}
	for (auto x: a) {
		t = t + x;
	}
	return t;
}

int sum_ranged(int[..] s) {
	int t = {0};
	t = 0;
	for (auto v: s) {
		t = t + v;
	}
	return t;
}

int sum_ranged_expr(void) {
	int a[3] = {0};
	int[..] s = {0};
	a[0] = 5;
	a[1] = 6;
	a[2] = 7;
	s = a;
	return sum_ranged(s);
}

int sum_nested(void) {
	int m[2][2] = {0};
	int t = {0};
	m[0][0] = 1;
	m[0][1] = 2;
	m[1][0] = 3;
	m[1][1] = 4;
	t = 0;
	for (auto row: m) {
		for (auto x: row) {
			t = t + x;
		}
	}
	return t;
}

int explicit_type(void) {
	int a[2] = {0};
	int64_t t = {0};
	a[0] = 10;
	a[1] = 20;
	t = 0;
	for (int64_t x: a) {
		t = t + x;
	}
	return (int)t;
}

int with_break(void) {
	int a[5] = {0};
	int t = {0};
	a[0] = 1;
	a[1] = 2;
	a[2] = 99;
	a[3] = 4;
	a[4] = 5;
	t = 0;
	for (auto x: a) {
		if (x == 99) {
			break;
		}
		t = t + x;
	}
	return t;
}

/* Pointer binding: mutate through auto *p / T *p. */
int ptr_bind_array(void) {
	int a[3] = {0};
	int t = {0};
	a[0] = 0;
	a[1] = 0;
	a[2] = 0;
	for (auto* p: a) {
		*p = 7;
	}
	t = 0;
	for (int* p: a) {
		t = t +*p;
	}
	return t;
}

int ptr_bind_ranged(void) {
	int a[4] = {0};
	int[..] s = {0};
	int t = {0};
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	s = a;
	for (auto* p: s) {
		*p = *p + 1;
	}
	t = 0;
	for (auto x: s) {
		t = t + x;
	}
	return t;
}

struct Pt {
	int x;
	int y;
};

int ptr_bind_struct(void) {
	Pt a[2] = {0};
	Pt[..] s = {0};
	int t = {0};
	s = a;
	for (auto* p: s) {
		p.x = 3;
		p.y = 4;
	}
	t = 0;
	for (auto* p: s) {
		t = t + p.x + p.y;
	}
	return t;
}
