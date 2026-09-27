// fail: F:implicit conversion from pointer to pointer requires a cast

#include <string.h>

int main() {
	char *p = strchr("hello", 'e');
	(void)p;
	return 0;
}
