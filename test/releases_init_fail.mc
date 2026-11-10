// fail: F:use of released value

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

int (Reg* r).val() {
	return r.base ? *r.base : 0;
}

void bad_auto_after_reset() {
	int g = 0;
	Reg r = { &g };
	r.reset();
	auto (n, ok) = r.next();
	(void)n;
	(void)ok;
}

void bad_init_after_free() {
	char* p = malloc(16);
	free(p);
	char* q = p;
	(void)q;
}

int bad_assign_method_after_reset() {
	int g = 0;
	Reg r = { &g };
	r.reset();
	return r.val();
}

char* take([[releases]] char* p) {
	return p;
}

char* bad_return_consume_twice() {
	char* p = malloc(16);
	p = take(p);
	return take(p);
}

int main() {
	return 0;
}
