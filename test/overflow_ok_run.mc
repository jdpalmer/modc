/* run: 0 */

int main() {
	int a = 1000;
	int b = 7;

	if (a + b != 1007) {
		return 1;
	}
	if (a - b != 993) {
		return 2;
	}
	if (a * b != 7000) {
		return 3;
	}
	if (-a != -1000) {
		return 4;
	}
	a++;
	if (a != 1001) {
		return 5;
	}
	return 0;
}
