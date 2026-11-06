/* Intentional OOB with runtime checks (default); must abort (non-zero). */
int main() {
	int a[2] = { 1, 2 };
	int[..] s = a;

	return s[2];
}
