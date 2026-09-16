/* (void)x silences unused parameter / local. */

int use_param(int x) {
	(void)x;
	return 1;
}

int use_local() {
	int y = { 0 };
	(void)y;
	return 1;
}
