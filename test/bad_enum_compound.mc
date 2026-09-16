enum Color { COLOR_RED };

int bad(void) {
	Color c = { 0 };
	c |= c;
	return c;
}
