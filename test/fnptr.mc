/* Function pointers: address-of and indirect calls */

int add(int a, int b) {
	return a + b;
}

int sub(int a, int b) {
	return a - b;
}

static int mul(int a, int b) {
	return a * b;
}

int callfp(int(*fp)(int, int), int a, int b) {
	return fp(a, b);
}

int callstar(int(*fp)(int, int), int a, int b) {
	return (*fp)(a, b);
}

int via_local() {
	int(*fp)(int, int) = { 0 };
	fp = add;
	return fp(20, 22);
}

int via_addr() {
	int(*fp)(int, int) = { 0 };
	fp = &add;
	return (*fp)(10, 32);
}

int via_static_decay() {
	int(*fp)(int, int) = { 0 };
	fp = mul;
	return fp(6, 7);
}

int via_static_addr() {
	int(*fp)(int, int) = { 0 };
	fp = &mul;
	return (*fp)(6, 7);
}

static int(*g_static_fp)(int, int) = mul;

int via_static_global() {
	return g_static_fp(3, 14);
}

int choose(int which, int a, int b) {
	int(*fp)(int, int) = { 0 };
	if (which) {
		fp = add;
	} else {
		fp = sub;
	}
	return fp(a, b);
}
