enum Color { COLOR_RED };

int bad() {
	Color c = { 0 };
	c |= c;
	return c;
}
