/* Subrange: s[lo..hi], open ends; exclusive end. */

int sum_elems(int[..] s) {
	int i = { 0 };
	int t = { 0 };
	t = 0;
	for (i = 0; i < (int)len(s); i++) {
		t = t + s[i];
	}
	return t;
}

int from_array_range() {
	int a[5] = { 0 };
	int[..] s = { 0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	a[4] = 5;
	s = a[1 .. 4];
	if (len(s) != 3) {
		return 1;
	}
	if (s[0] != 2 || s[2] != 4) {
		return 2;
	}
	if (sum_elems(s) != 9) {
		return 3;
	}
	return 0;
}

int open_ends() {
	int a[4] = { 0 };
	int[..] s = { 0 };
	int[..] t = { 0 };
	a[0] = 10;
	a[1] = 20;
	a[2] = 30;
	a[3] = 40;
	s = a[2 ..];
	if (len(s) != 2 || s[0] != 30 || s[1] != 40) {
		return 1;
	}
	s = a[.. 2];
	if (len(s) != 2 || s[0] != 10 || s[1] != 20) {
		return 2;
	}
	s = a[..];
	if (len(s) != 4 || sum_elems(s) != 100) {
		return 3;
	}
	t = s[1 .. 3];
	if (len(t) != 2 || t[0] != 20 || t[1] != 30) {
		return 4;
	}
	t = t[..];
	if (len(t) != 2) {
		return 5;
	}
	return 0;
}

int ranged_sub_run() {
	if (from_array_range() != 0) {
		return 1;
	}
	if (open_ends() != 0) {
		return 2;
	}
	return 0;
}
