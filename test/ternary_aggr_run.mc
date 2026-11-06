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
	e.file_name = ranged("ab", 2);
	char[..] a = pick(&e);
	if (len(a) != 2 || a[0] != 'a') {
		return 1;
	}
	char[..] b = pick(NULL);
	if (len(b) != 0) {
		return 2;
	}
	/* Both arms locals (same representation) */
	{
		char[..] x = ranged("xy", 2);
		int c = { 1 };
		char[..] y = c ? x: empty();
		if (len(y) != 2) {
			return 3;
		}
	}
	return 0;
}
