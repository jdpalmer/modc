/* Single-file program for modc build/run CLI smoke test. */

int add(int a, int b) {
	return a + b;
}

int main(void) {
	return add(20, 22) == 42 ? 0: 1;
}
