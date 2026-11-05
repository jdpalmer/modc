/* Intentional OOB with runtime checks (default); must abort (non-zero). */
int main() {
	int a[2] = { 1, 2 };
	int[..] s = { 0 };

	s = a;
	return s[2];
}
