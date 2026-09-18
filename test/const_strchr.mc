#include <string.h>

/* Host <string.h> search APIs use const? (passthrough). */

int const_strchr_ok() {
	const char *c = "hello";
	char buf[8] = { 0 };
	char *m = { 0 };
	const char *pc = { 0 };
	char *pm = { 0 };

	buf[0] = 'h';
	buf[1] = 'e';
	buf[2] = 'l';
	buf[3] = 'l';
	buf[4] = 'o';
	m = buf;
	pc = strchr(c, 'e');
	pm = strchr(m, 'e');
	if (pc == 0 || pm == 0) {
		return 1;
	}
	if (strstr(c, "ll") == 0) {
		return 2;
	}
	if (memchr(c, 'h', 5) == 0) {
		return 3;
	}
	pm[0] = 'E';
	if (buf[1] != 'E') {
		return 4;
	}
	(void)strrchr(c, 'l');
	(void)strpbrk(m, "lo");
	return 0;
}
