/* Pointer-only unions and typed pointer arithmetic remain legal. */

union Ptrs {
	void* any;
	char* s;
};

int ptr_union_ok(void) {
	union Ptrs u = {0};
	char* p = {0};
	char buf[4] = {0};
	u.s = buf;
	u.any = u.s;
	p = (char*)u.any;
	p = p + 1;
	(void)p;
	return 0;
}
