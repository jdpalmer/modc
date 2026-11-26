/* Address-of selection distinguishes const params. */
#include <assert.h>

overload int lenof(char[..] s) {
	(void)s;
	return 1;
}

overload int lenof(const char[..] s) {
	(void)s;
	return 2;
}

int main() {
	int (*pm)(char[..]) = lenof;
	int (*pc)(const char[..]) = &lenof;
	char buf[1] = { 'x' };

	assert_eq(pm(buf[0..1]), 1);
	assert_eq(pc("hi"), 2);
	return 0;
}
