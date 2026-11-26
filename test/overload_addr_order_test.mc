/* Address-of must not depend on declaration order. */
#include <assert.h>

overload float max(float a, float b) {
	(void)a;
	(void)b;
	return 22.0f;
}

overload int max(int a, int b) {
	(void)a;
	(void)b;
	return 11;
}

int main() {
	int (*pi)(int, int) = &max;
	float (*pf)(float, float) = max;

	assert_eq(pi(0, 0), 11);
	assert_eq(pf(0.0f, 0.0f), 22.0f);
	return 0;
}
