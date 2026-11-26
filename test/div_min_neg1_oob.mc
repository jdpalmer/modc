/* Signed MIN / -1 with checks on; must abort (non-zero). */

int main() {
	int a = -2147483647 - 1;
	int b = -1;

	return a / b;
}
