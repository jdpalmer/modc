/* Arrays of arrays (nested fixed-size arrays) are supported. */

int get22(void) {
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
