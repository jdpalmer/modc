// fail: F:'gets' is not allowed in %C

#include <stdio.h>

int main() {
	char buf[32] = { 0 };
	gets(buf);
	return 0;
}
