// fail: F:address of overloaded function max requires a function pointer type

overload int max(int a, int b) {
	(void)a;
	(void)b;
	return 0;
}

overload float max(float a, float b) {
	(void)a;
	(void)b;
	return 0;
}

int main() {
	void *p = (1 ? max : max);
	(void)p;
	return 0;
}
