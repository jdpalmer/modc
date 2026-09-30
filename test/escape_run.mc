/* C simple escapes must decode, not fall through as the letter. */

int main() {
	char bel = '\a';
	char bs = '\b';
	char ff = '\f';
	char vt = '\v';
	char hex = '\x07';
	char oct = '\007';
	const char *s = "\a\b\f\v";

	if (bel != 7) {
		return 1;
	}
	if (bs != 8) {
		return 2;
	}
	if (ff != 12) {
		return 3;
	}
	if (vt != 11) {
		return 4;
	}
	if (hex != 7) {
		return 5;
	}
	if (oct != 7) {
		return 6;
	}
	if (s[0] != 7 || s[1] != 8 || s[2] != 12 || s[3] != 11 || s[4] != 0) {
		return 7;
	}
	if ("\a"[0] == 'a') {
		return 8;
	}
	return 0;
}
