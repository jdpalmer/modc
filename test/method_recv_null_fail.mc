// fail: F:cannot compare method receiver 'b' to null; callers must pass a live object

/* Method receivers: no null tests, no rebind. */
#include <stddef.h>

struct Buf {
	int n;
};

void (Buf* b).clear() {
	if (b == NULL) {
		return;
	}
	b.n = 0;
}

int main() {
	return 0;
}
