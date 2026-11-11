// fail: F:'sprintf' is not allowed in %C

#include <stdio.h>

int main() {
	char buf[32] = { 0 };
	sprintf(buf, "%d", 1);
	return 0;
}
