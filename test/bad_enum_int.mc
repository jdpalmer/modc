enum Color {
	COLOR_RED, COLOR_GREEN };

enum Shape {
	SHAPE_CIRCLE };

int bad(void) {
	Color c = {0};
	c = 1;
	return c;
}
