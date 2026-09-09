#include <stdio.h>

/* sum of a and b */
int add(int a, int b) {
	if (a > b) {
		return a;
	} else {
		return b;
	}
}

int mul(int* p, int n) {
	int i;
	int s;
	s = 0;
	for (i = 0; i < n; i++) {
		s = s + p[i];
	}
	return s;
}
