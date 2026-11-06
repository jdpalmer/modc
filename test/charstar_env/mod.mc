/* Package used by charstar_ranged_test: environ → char*[..] → char* without cast. */
#include <unistd.h>
#include <string.h>

int charstar_env_scan() {
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
	int i = 0;
	for (; i < n; i++) {
		char* row = es[i];
		char* eq = strchr(row, '=');
		if (eq == 0) {
			continue;
		}
		(void)eq;
	}
	return n;
}
