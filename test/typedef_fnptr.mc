/* Function-pointer typedefs must surviveprescan + main parse without redefinition. */
typedef bool(*Handler)(bool, int);
typedef int(*BinOp)(int, int);

int add(int a, int b) {
	return a + b;
}

bool ok(bool a, int b) {
	(void)b;
	return a;
}

int typedef_fnptr_run() {
	BinOp op = { 0 };
	Handler h = { 0 };
	op = add;
	h = ok;
	if (op(20, 22) != 42) {
		return 1;
	}
	if (!h(true, 0)) {
		return 2;
	}
	return 0;
}
