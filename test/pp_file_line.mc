/* Predefined __FILE__ / __LINE__ (invocation site inside macros). */

#include <string.h>

#define WRAP_LINE() __LINE__

int file_line_tests() {
	int a = {0};
	int b = {0};
	const char *f = {0};
#if __LINE__ < 1
	return 1;
#endif
	a = __LINE__;
	if (__LINE__ != a + 1) {
		return 2;
	}
	a = WRAP_LINE();
	b = WRAP_LINE();
	if (b != a + 1) {
		return 3;
	}
	f = __FILE__;
	if (f == 0 || f[0] == 0) {
		return 4;
	}
	if (strstr(f, "pp_file_line") == 0) {
		return 5;
	}
	return 0;
}
