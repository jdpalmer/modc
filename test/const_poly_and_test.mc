/* Multi-param const? return uses AND across poly args. */
#include <assert.h>

const? char[..] pick(const? char[..] a, const? char[..] b) {
	(void)b;
	return a;
}

int main() {
	char buf[2] = { 'x', 'y' };
	const char[..] lit = "hi";
	char[..] m = buf[0..2];
	const? char[..] both_const = pick(lit, "ab");
	char[..] mixed = pick(m, lit);

	assert_eq(len(both_const), 2);
	assert_eq(len(mixed), 2);
	mixed[0] = 'z';
	return 0;
}
