/* Extra parens allow intentional assignment in a condition. */

int ok_if(void) {
	int x = { 0 };
	x = 1;
	if ((x = 0)) {
		return 1;
	}
	if (((x = 2))) {
		return x;
	}
	return 0;
}

int ok_cmp(void) {
	int x = { 0 };
	x = 1;
	if (x == 1) {
		return 1;
	}
	return 0;
}
