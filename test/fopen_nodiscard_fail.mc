// fail: F:discarded [[nodiscard]] return value

#include <stdio.h>

int main() {
	fopen("/dev/null", "r");
	return 0;
}
