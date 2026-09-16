/* Empty () means zero parameters; typed prototypes are still required. */

int empty_parens() {
	return 1;
}

int another_empty() {
	return empty_parens();
}

int one_arg(int x) {
	return x + 1;
}

int call_ok() {
	return one_arg(41);
}
