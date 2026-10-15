/* In-bounds T[N], T[..], fixed-array params, and subranges under --bounds-check. */
#include <assert.h>

static int sum4(int a[4]) {
	int i = 0;
	int t = 0;

	for (i = 0; i < 4; i++) {
		t += a[i];
	}
	return t;
}

static int mid_sum(int a[4]) {
	int[..] m = { 0 };
	int i = 0;
	int t = 0;

	m = a[1..3];
	for (i = 0; i < (int)len(m); i++) {
		t += m[i];
	}
	return t;
}

int main() {
	int a[4] = { 1, 2, 3, 4 };
	int[..] s = { 0 };
	int[..] w = { 0 };
	int i = 0;

	s = a;
	assert_eq(a[0], 1);
	assert_eq(a[3], 4);
	assert_eq(s[0], 1);
	assert_eq(s[3], 4);
	for (i = 0; i < (int)len(s); i++) {
		assert_eq(s[i], a[i]);
	}
	assert_eq(sum4(a), 10);
	assert_eq(mid_sum(a), 5);
	w = a[1..3];
	assert_eq(len(w), 2);
	assert_eq(w[0], 2);
	assert_eq(w[1], 3);
	w = s[0..4];
	assert_eq(len(w), 4);
	w = a[2..];
	assert_eq(len(w), 2);
	assert_eq(w[0], 3);
	w = a[4..4];
	assert_eq(len(w), 0);
	return 0;
}
