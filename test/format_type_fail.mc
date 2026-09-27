// fail: F:format specifies an integer but the argument has type array

#include <stdio.h>

int main() {
	printf("%d", "x");
	return 0;
}
