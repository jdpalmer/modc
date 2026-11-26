/* Runtime signed << where the result does not fit; must abort. */

int main() {
	int a = 1;
	int b = 31;

	return a << b;
}
