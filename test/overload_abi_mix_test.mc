/* Unmarked C ABI entry plus overload extras; contextual & selection. */
#include <assert.h>

int max(int a, int b) {
	return a > b ? a : b;
}

overload float max(float a, float b) {
	return a > b ? a : b;
}

int main() {
	int (*pi)(int, int) = max;
	float (*pf)(float, float) = &max;
	int (*pj)(int, int) = (int (*)(int, int))max;

	assert_eq(max(1, 2), 2);
	assert_eq(max(1.0f, 2.0f), 2.0f);
	assert_eq(pi(3, 4), 4);
	assert_eq(pf(3.0f, 4.0f), 4.0f);
	assert_eq(pj(5, 6), 6);
	return 0;
}
