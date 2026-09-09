char* leaf_lit(void) {
	return "x";
}

char* mid_wrap(void) {
	return leaf_lit();
}

int autoconst_return_transitive_ok(void) {
	char* p = {0};
	p = mid_wrap();
	return p[0] == (char)'x' ? 0: 1;
}
