// fail: F:overload of max collides with the unmarked C ABI declaration

int max(int a, int b) {
	(void)a;
	(void)b;
	return 0;
}

overload int max(int a, int b) {
	(void)a;
	(void)b;
	return 0;
}

int main() {
	return max(1, 2);
}
