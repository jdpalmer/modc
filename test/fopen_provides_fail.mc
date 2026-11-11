// fail: F:owned value 'f' never released or retained

#include <stdio.h>

int main() {
	FILE* f = fopen("/dev/null", "r");

	(void)f;
	return 0;
}
