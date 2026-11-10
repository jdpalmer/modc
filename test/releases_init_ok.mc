// ok:

#include <stdlib.h>

struct Reg {
	int* base;
};

void ([[releases]] Reg* r).reset() {
	(void)r;
}

(int, bool) (Reg* r).next() {
	(void)r;
	return (0, false);
}

int main() {
	int g = 0;
	Reg r = { &g };
	auto (n, ok) = r.next();
	(void)n;
	(void)ok;
	r.reset();
	return 0;
}
