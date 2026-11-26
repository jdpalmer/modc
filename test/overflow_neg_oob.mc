/* Unary negation of INT_MIN; must abort with checks on. */

int main() {
	int a = -2147483647 - 1;

	return -a;
}
