/* Predefined __FILE__ / __LINE__ (invocation site inside macros). */

#include <string.h>

#define WRAP_LINE() __LINE__

int file_line_tests() {
	int a = __LINE__;
	const char *f = __FILE__;
#if __LINE__ < 1
	return 1;
#endif
	if (__LINE__ != a + 1) {
		return 2;
	}
	a = WRAP_LINE();
	{
		int b = WRAP_LINE();
		if (b != a + 1) {
			return 3;
		}
	}
	if (f == 0 || *f == 0) {
		return 4;
	}
	if (strstr(f, "pp_file_line") == 0) {
		return 5;
	}
	return 0;
}
