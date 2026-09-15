/* range_at-only types reject for (auto *p : …). */

#include <stddef.h>

typedef struct Triple Triple;

struct Triple {
	int a;
	int b;
	int c;
};

overload size_t range_count(Triple t, int** out_ptr) {
	(void)t;
	*out_ptr = 0;
	return 3;
}

overload int range_at(Triple t, size_t i) {
	if (i == 0) {
		return t.a;
	}
	if (i == 1) {
		return t.b;
	}
	return t.c;
}

int main(void) {
	Triple t = { 0 };
	t.a = 1;
	t.b = 2;
	t.c = 3;
	for (auto* p: t) {
		(void)p;
	}
	return 0;
}
