/* run: 0 */

int main() {
	int a = 42;
	int b = 6;

	if (a / b != 7) {
		return 1;
	}
	if (a % b != 0) {
		return 2;
	}
	a /= 7;
	if (a != 6) {
		return 3;
	}
	return 0;
}
