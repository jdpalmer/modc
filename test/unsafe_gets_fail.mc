// fail: F:'gets' is deprecated

#include <stdio.h>

int main() {
	char buf[32] = { 0 };
	gets(buf);
	return 0;
}
