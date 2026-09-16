enum Color {
	COLOR_RED,
	COLOR_GREEN
};

Color bad(bool pick) {
	return pick ? COLOR_RED: 1;
}
