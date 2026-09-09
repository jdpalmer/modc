/* Locals, assignment, and calls */

int add(int a, int b) {
	return a + b;
}

int use_local(int a, int b) {
	int x = {0};
	x = a + b;
	return x;
}

int call_add(int a, int b) {
	int x = {0};
	x = add(a, b);
	return x;
}

int ptr_store(int* p, int v) {
	*p = v;
	return*p;
}
