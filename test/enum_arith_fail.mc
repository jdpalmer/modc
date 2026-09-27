// fail: F:operator '+' is not allowed on an enum; cast explicitly

enum Color { COLOR_RED };

int bad() {
	Color c = { 0 };
	return c + 1;
}
