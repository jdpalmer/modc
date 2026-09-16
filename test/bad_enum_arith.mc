enum Color { COLOR_RED };

int bad(void) {
	Color c = { 0 };
	return c + 1;
}
