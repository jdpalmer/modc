// fail: F:'strcpy' is deprecated

#include <string.h>

int main() {
	char dst[8] = { 0 };
	strcpy(dst, "hi");
	return 0;
}
