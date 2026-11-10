/* Pointer-only unions and typed pointer arithmetic remain legal. */

union Ptrs {
	void* any;
	char* s;
};

int ptr_union_ok() {
	char buf[4] = { 0 };
	union Ptrs u = { buf };
	{
		char* p = (char*)u.any;
		p = p + 1;
		(void)p;
	}
	return 0;
}
