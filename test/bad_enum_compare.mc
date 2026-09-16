enum Color { COLOR_RED };
enum Shape { SHAPE_CIRCLE };

int bad() {
	Color c = { 0 };
	Shape s = { 0 };
	return c == s;
}
