/* In-bounds T[N] and T[..] indexing under --bounds-check. */
#include <assert.h>

int main() {
	int a[4] = { 1, 2, 3, 4 };
	int[..] s = { 0 };
	int i = 0;

	s = a;
	assert_eq(a[0], 1);
	assert_eq(a[3], 4);
	assert_eq(s[0], 1);
	assert_eq(s[3], 4);
	for (i = 0; i < (int)len(s); i++) {
		assert_eq(s[i], a[i]);
	}
	return 0;
}
