/* Tagged enums are distinct types: widen to int; back via cast only. */

enum Color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

enum Shape {
	SHAPE_CIRCLE,
	SHAPE_SQUARE
};

int as_int(Color c) {
	return c;
}

Color from_int(int i) {
	return (Color)i;
}

int same_enum(Color a, Color b) {
	return a == b;
}

Color choose_color(bool pick) {
	return pick ? COLOR_RED: COLOR_BLUE;
}

int enum_order(Color a, Color b) {
	return a < b;
}

int green_is_one() {
	Color c = { 0 };
	c = COLOR_GREEN;
	return as_int(c) == 1 && from_int(1) == COLOR_GREEN && same_enum(COLOR_GREEN, COLOR_GREEN) && choose_color(false) == COLOR_BLUE && enum_order(COLOR_RED, COLOR_GREEN);
}

int mix_ok_via_int() {
	Color c = { 0 };
	Shape s = { 0 };
	c = COLOR_RED;
	s = SHAPE_CIRCLE;
	return (int)c == (int)s;
}
