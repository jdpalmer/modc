// fail: F:array subscript must be an integer

int bad_float_subscript() {
	int values[2] = {
		1, 2 };
	return values[1.5];
}
