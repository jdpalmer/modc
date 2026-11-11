// fail: F:'strcat' is deprecated

#include <string.h>

int main() {
	char dst[8] = { 'a', 0 };
	strcat(dst, "b");
	return 0;
}
