#include <stdint.h>

overload void g(int a, int64_t b) {
	(void)a;
	(void)b;
}
overload void g(int64_t a, int b) {
	(void)a;
	(void)b;
}

int main(void) {
	g(1, 1);
	return 0;
}
