// fail: F:passing stack address to [[retained]] parameter 'p' of 'keep'

int* slot;

void keep([[retained]] int* p) {
	slot = p;
}

int bad() {
	int x = 0;
	keep(&x);
	return 0;
}
