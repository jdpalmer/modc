/* run: 0 */

int main() {
	int a = 3;
	unsigned u = (unsigned)-1;

	if ((a << 2) != 12) {
		return 1;
	}
	if ((u << 1) == 0 && u != 0) {
		/* unsigned wrap of shift is fine; just exercise the path */
	}
	a <<= 1;
	if (a != 6) {
		return 2;
	}
	return 0;
}
