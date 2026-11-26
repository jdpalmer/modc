// fail: F:ambiguous call to method g

#include <stdint.h>

struct Box {
	int n;
};

int (Box* b).g(int a, int64_t c) {
	return b.n + a + (int)c;
}

int (Box* b).g(int64_t a, int c) {
	return b.n + (int)a + c;
}

int main() {
	Box b = { 1 };
	return b.g(1, 1);
}
