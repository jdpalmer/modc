// fail: F:scanf '%s' requires a mutable char *

#include <stdio.h>

int main() {
	const char *p = "hi";
	scanf("%s", p);
	return 0;
}
