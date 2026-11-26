// fail: F:no overload of max matches the target function type

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
	double (*p)(double, double) = &max;
	(void)p;
	return 0;
}
