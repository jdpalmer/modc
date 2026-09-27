// fail: F:pointer compound assignment requires an integer offset

int bad_pointer_offset(int* p) {
	p += 1.5;
	return 0;
}
