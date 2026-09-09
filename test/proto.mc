/* Empty () is (void); typed prototypes required. */

int empty_parens(void) {
	return 1;
}

int explicit_void(void) {
	return empty_parens();
}

int one_arg(int x) {
	return x + 1;
}

int call_ok(void) {
	return one_arg(41);
}
