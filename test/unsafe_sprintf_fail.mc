// fail: F:'sprintf' is deprecated

#include <stdio.h>

int main() {
	char buf[32] = { 0 };
	sprintf(buf, "%d", 1);
	return 0;
}
