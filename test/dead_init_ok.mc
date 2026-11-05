int partial(int c) {
	int x = 0;
	if (c) {
		x = 1;
	}
	return x;
}

int used() {
	int x = 3;
	return x;
}

int reassign_after_read(int c) {
	int x = 0;
	if (c) {
		return x;
	}
	x = 1;
	return x;
}

int addr_escape() {
	int x = 0;
	int* p = &x;
	*p = 9;
	return x;
}

int main() {
	(void)partial(0);
	(void)used();
	(void)reassign_after_read(1);
	(void)addr_escape();
	return 0;
}
