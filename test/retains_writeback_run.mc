#include <stddef.h>
#include <stdio.h>

struct Cur {
	char* p;
};

static void adv(Cur* c) {
	c.p = c.p + 1;
}

static void swap_adj(int[..] a, size_t i, size_t j) {
	int t = a[i];
	a[i] = a[j];
	a[j] = t;
}

int main() {
	char buf[4] = { 'a', 'b', 'c', 0 };
	Cur cur = { buf };
	int arr[2] = { 1, 2 };

	adv(&cur);
	if (*cur.p != 'b') {
		return 1;
	}
	swap_adj(arr, 0, 1);
	if (arr[0] != 2 || arr[1] != 1) {
		return 1;
	}
	printf("ok\n");
	return 0;
}
