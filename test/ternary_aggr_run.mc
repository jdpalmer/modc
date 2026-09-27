/* Ternary with aggregate arms (field vs call) must not emit =@ phi. */
#include <stddef.h>

struct Ent {
	int tag;
	char[..] file_name;
};

static char[..] empty() {
	char[..] e = { 0 };
	return e;
}

char[..] pick(Ent* cur) {
	return cur != NULL ? cur.file_name: empty();
}

int ternary_aggr_run() {
	Ent e = { 0 };
	char[..] a = { 0 };
	char[..] b = { 0 };
	e.file_name = ranged("ab", 2);
	a = pick(&e);
	if (len(a) != 2 || a[0] != 'a') {
		return 1;
	}
	b = pick(NULL);
	if (len(b) != 0) {
		return 2;
	}
	/* Both arms locals (same representation) */
	{
		char[..] x = { 0 };
		char[..] y = { 0 };
		int c = { 1 };
		x = ranged("xy", 2);
		y = c ? x: empty();
		if (len(y) != 2) {
			return 3;
		}
	}
	return 0;
}
