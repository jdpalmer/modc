/* Intentional OOB under --bounds-check; must abort (non-zero). */
int main() {
	int a[2] = { 1, 2 };
	int[..] s = { 0 };
	int x = 0;

	s = a;
	x = s[2];
	return x;
}
