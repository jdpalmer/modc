// fail: F:implicit conversion from pointer to pointer requires a cast

int bad(int* p) {
	char* b = { 0 };
	b = p;
	return b != 0;
}
