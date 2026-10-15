/* Fixed-array param OOB under --bounds-check; must abort (non-zero). */
static int grab(int a[2]) {
	return a[2];
}

int main() {
	int a[2] = { 1, 2 };

	return grab(a);
}
