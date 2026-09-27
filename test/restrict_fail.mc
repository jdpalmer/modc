// fail: F:restrict is not used in %C (allowed in headers)

int bad(int* restrict p) {
	return*p;
}
