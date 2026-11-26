/* Signed mul overflow; must abort with checks on. */

int main() {
	int a = 46341;
	int b = 46341;

	return a * b;
}
