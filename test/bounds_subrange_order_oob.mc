/* Subrange with lo > hi with runtime checks (default); must abort (non-zero). */
int main() {
	int a[4] = { 1, 2, 3, 4 };
	int lo = 3;
	int[..] w = a[lo..1];

	return (int)len(w);
}
