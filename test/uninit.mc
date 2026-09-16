/* Locals must be initialized at declaration. */

int init_decl() {
	int x = 3;
	return x;
}

int init_assign() {
	int x = 4;
	return x;
}

int both_branches(int c) {
	int x = c ? 1: 2;
	return x;
}

int do_assigns() {
	int x = 3;
	int n = 3;
	do {
		x = n;
		n--;
	}
	while (n > 0);
	return x;
}

int addr_escape() {
	int x = 0;
	int* p = &x;
	*p = 9;
	return x;
}
