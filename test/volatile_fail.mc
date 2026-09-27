// fail: F:volatile is not used in %C (allowed in headers)

int bad(volatile int* p) {
	return*p;
}
