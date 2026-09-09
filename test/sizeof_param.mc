/* sizeof on a real array or pointer param is fine; array params decay. */

int sizeof_local(void) {
	int a[4] = {0};
	return (int) sizeof (a);
}

int sizeof_ptr(int* p) {
	return (int) sizeof (p);
}

int sizeof_elem(int a[8]) {
	return (int) sizeof (a[0]);
}
