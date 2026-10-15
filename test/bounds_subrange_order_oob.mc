/* Subrange with lo > hi under --bounds-check; must abort (non-zero). */
int main() {
	int a[4] = { 1, 2, 3, 4 };
	int[..] w = { 0 };
	int lo = 3;

	w = a[lo..1];
	return (int)len(w);
}
