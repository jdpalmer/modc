// fail: F:no overload of lenof matches the target function type

overload int lenof(char[..] s) {
	(void)s;
	return 0;
}

int main() {
	int (*pc)(const char[..]) = &lenof;
	(void)pc;
	return 0;
}
