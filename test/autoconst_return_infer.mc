char* lit(void) {
	return "x";
}

int autoconst_return_infer_ok(void) {
	char* p = { 0 };
	p = lit();
	return p[0] == (char)'x' ? 0: 1;
}
