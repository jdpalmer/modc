// fail: F:passing stack address to [[retains]] parameter

#include <stddef.h>

int* g;

struct Holder {
	int* p;
};

struct Cur {
	char* p;
};

void stash_global(int* p) {
	g = p;
}

void stash_into(Holder* h, int* p) {
	h.p = p;
}

void set_field(Cur* c, char* other) {
	c.p = other;
}

void bad_global() {
	int x = 0;
	stash_global(&x);
}

void bad_into() {
	Holder h = { 0 };
	int x = 0;
	stash_into(&h, &x);
}

void bad_other() {
	Cur c = { 0 };
	char buf[4] = { 0 };
	char* p = buf;
	set_field(&c, p);
}

int main() {
	return 0;
}
