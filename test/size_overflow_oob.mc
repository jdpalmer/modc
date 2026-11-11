/* Intentional count*elemsz overflow; must abort (non-zero) with size: … */
#include <stddef.h>

int main() {
	size_t n = (~(size_t)0) / sizeof(int) + 1;

	alloc(int, n);
	return 0;
}
