/* char*[..] element type is char*; no cast needed (same pattern as os/unix.mc). */
#include <unistd.h>
#include <string.h>

int charstar_ranged_ok() {
	char** e = environ;
	int n = 0;
	if (e) {
		char** p = e;
		while (*p) {
			n++;
			p++;
		}
	}
	char*[..] es = ranged(e, n);
	if (n == 0) {
		return 0;
	}
	char* row = es[0];
	return row != 0 && strchr(row, '=') != 0 ? 1 : 0;
}
