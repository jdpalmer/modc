char* leaf_lit() {
	return "x";
}

char* mid_wrap() {
	return leaf_lit();
}

int autoconst_return_transitive_ok() {
	char* p = { 0 };
	p = mid_wrap();
	return p[0] == (char)'x' ? 0: 1;
}
