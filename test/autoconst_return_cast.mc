char* leak_cast(void) {
	return (char*)"x";
}

int autoconst_return_cast_ok(void) {
	char* p = {0};
	p = leak_cast();
	return p[0] == (char)'x' ? 0: 1;
}
