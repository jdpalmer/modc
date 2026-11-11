// fail: F:discarded [[nodiscard]] return value

#include <stdlib.h>

int main() {
	malloc(8);
	return 0;
}
