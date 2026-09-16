enum Color { COLOR_RED };

int bad() {
	Color c = { 0 };
	return c + 1;
}
