/* Signed INT_MAX+1; traps with checks, wraps under --release. */
#include <stdio.h>

int main() {
	int a = 2147483647;
	int b = 1;
	int s = a + b;

	printf("wrapped %d\n", s);
	return 0;
}
