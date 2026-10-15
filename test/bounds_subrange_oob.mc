/* Subrange past parent len under --bounds-check; must abort (non-zero). */
int main() {
	int a[2] = { 1, 2 };
	int[..] w = { 0 };

	w = a[0..3];
	return (int)len(w);
}
