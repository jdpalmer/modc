/* Runtime left shift of a negative signed value; must abort. */

int main() {
	int a = -1;
	int b = 1;

	return a << b;
}
