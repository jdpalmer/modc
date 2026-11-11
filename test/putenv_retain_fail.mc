// fail: F:passing stack address to [[retains]] parameter

#include <stdlib.h>

int main() {
	char buf[8] = { 'A', '=', '1', 0 };

	putenv(buf);
	return 0;
}
