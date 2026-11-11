// fail: F:format string must be a string literal

#include <stdio.h>

int main() {
	const char* fmt = "%d";
	printf(fmt, 1);
	return 0;
}
