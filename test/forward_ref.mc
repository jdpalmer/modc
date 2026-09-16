int call_before_def() {
	return empty_parens();
}

int empty_parens() {
	return 1;
}

int mut_b(int n) {
	if (n <= 0) {
		return 0;
	}
	return mut_a(n - 1) + 1;
}

int mut_a(int n) {
	return mut_b(n);
}

int forward_ref_ok() {
	if (call_before_def() != 1) {
		return 1;
	}
	if (mut_a(2) != 2) {
		return 2;
	}
	return 0;
}
