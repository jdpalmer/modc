/* Portable test (was nestarr.mc + nestarr_main.c). */
#include <assert.h>

/* Arrays of arrays (nested fixed-size arrays) are supported. */

int get22() {
	int a[2][3] = { 0 };
	a[0][0] = 1;
	a[0][1] = 2;
	a[0][2] = 3;
	a[1][0] = 4;
	a[1][1] = 5;
	a[1][2] = 6;
	return a[1][2];
}

int sum2d(int a[2][3]) {
	int i = { 0 };
	int j = { 0 };
	int s = { 0 };
	s = 0;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++) {
			s = s + a[i][j];
		}
	}
	return s;
}

int main() {

	int a[2][3] = { 0 };
	int i = { 0 };
	int j = { 0 };
	int n = { 0 };

	assert_eq(get22(), 6);
	n = 1;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++) {
			a[i][j] = n;
			n++;
		}
	}
	assert_eq(sum2d(a), 21);
	return 0;
}
