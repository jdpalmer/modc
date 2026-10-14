/* Distinct ranged overloads must not share a mangled link name. */
#include <assert.h>

overload int tag_of(int[..] a) {
	(void)a;
	return 1;
}

overload int tag_of(char[..] a) {
	(void)a;
	return 2;
}

int main() {
	int ia[2] = { 0 };
	char ca[2] = { 0 };

	assert_eq(tag_of(ia), 1);
	assert_eq(tag_of(ca), 2);
	return 0;
}
