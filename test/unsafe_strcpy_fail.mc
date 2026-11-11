// fail: F:'strcpy' is not allowed in %C

#include <string.h>

int main() {
	char dst[8] = { 0 };
	strcpy(dst, "hi");
	return 0;
}
