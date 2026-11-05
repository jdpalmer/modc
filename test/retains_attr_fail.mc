// fail: F:passing stack address to [[retains]] parameter 'p' of 'keep'

int* slot;

void keep([[retains]] int* p) {
	slot = p;
}

int bad() {
	int x = 0;
	keep(&x);
	return 0;
}
