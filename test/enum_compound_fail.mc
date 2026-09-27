// fail: F:compound assignment with an enum operand is not allowed; cast explicitly

enum Color { COLOR_RED };

int bad() {
	Color c = { 0 };
	c |= c;
	return c;
}
