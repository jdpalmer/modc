/* sizeof on a real array or pointer param is fine; array params decay. */

int sizeof_local() {
	int a[4] = { 0 };
	return sizeof(a);
}

int sizeof_ptr(int* p) {
	return sizeof(p);
}

int sizeof_elem(int a[8]) {
	return sizeof(a[0]);
}
