/* static_assert / _Static_assert — compile-time checks via eval_const. */

struct Point {
	int x;
	int y;
};

enum Color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

static_assert(sizeof(Point) == 8, "Point layout");
static_assert((int)COLOR_BLUE == 2);
_Static_assert(1 + 1 == 2, "arith");

int in_block(void) {
	static_assert(sizeof(int) == 4);
	return 0;
}

int ok(void) {
	return in_block() == 0 && sizeof(Point) == 8;
}
