/* Function pointers: address-of and indirect calls */

int add(int a, int b) {
	return a + b;
}

int sub(int a, int b) {
	return a - b;
}

int callfp(int(*fp)(int, int), int a, int b) {
	return fp(a, b);
}

int callstar(int(*fp)(int, int), int a, int b) {
	return (*fp)(a, b);
}

int via_local(void) {
	int(*fp)(int, int) = {0};
	fp = add;
	return fp(20, 22);
}

int via_addr(void) {
	int(*fp)(int, int) = {0};
	fp = &add;
	return (*fp)(10, 32);
}

int choose(int which, int a, int b) {
	int(*fp)(int, int) = {0};
	if (which) {
		fp = add;
	} else {
		fp = sub;
	}
	return fp(a, b);
}
