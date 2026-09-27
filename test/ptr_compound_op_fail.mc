// fail: F:only += and -= are allowed as pointer compound assignments

int bad_pointer_compound(int* p) {
	p *= 2;
	return 0;
}
