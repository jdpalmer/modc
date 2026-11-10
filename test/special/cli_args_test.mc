/* main(const char[..][..] args) — hosted argc/argv shim; each args[i] is a
 * view into argv (NUL excluded from len/cap). */
int main(const char[..][..] args) {
	if (len(args) != 3) {
		return 1;
	}
	if (len(args[1]) != 1 || args[1][0] != 'a') {
		return 2;
	}
	if (len(args[2]) != 1 || args[2][0] != 'b') {
		return 3;
	}
	return 0;
}
