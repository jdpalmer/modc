int bad(void* p) {
	p = p + 1;
	return p != 0;
}
