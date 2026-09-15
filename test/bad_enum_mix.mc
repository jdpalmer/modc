enum Color { COLOR_RED };

enum Shape { SHAPE_CIRCLE };

int bad(void) {
	Color c = { 0 };
	Shape s = { 0 };
	c = COLOR_RED;
	s = c;
	return (int)s;
}
