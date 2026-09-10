/* String-literal array bounds use decoded size, not source escape spelling. */

int str_escape_len_run(void) {
	char esc[4] = "\x1b[K";

	if (len("\x1b[K") != 4) {
		return 1;
	}
	if (len("\x41") != 2) {
		return 2;
	}
	if (len("\n") != 2) {
		return 3;
	}
	if (len("hi") != 3) {
		return 4;
	}
	if (len("\033[K") != 4) {
		return 5;
	}
	if (len(esc) != 4) {
		return 6;
	}
	if (esc[0] != 27 || esc[1] != '[' || esc[2] != 'K' || esc[3] != 0) {
		return 7;
	}
	if (len(ranged("\x1b[K")) != 3) {
		return 8;
	}
	return 0;
}
