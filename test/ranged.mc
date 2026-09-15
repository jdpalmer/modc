/* Ranged array int[..]: opaque header; use len()/cap()/ptr(). */

int sum_elems(int[..] s) {
	int i = { 0 };
	int t = { 0 };
	t = 0;
	for (i = 0; i < (int)len(s); i++) {
		t = t + s[i];
	}
	return t;
}

int[..] id_ranged(int[..] s) {
	return s;
}

int from_array(void) {
	int a[4] = { 0 };
	int[..] s = { 0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	s = a;
	if (len(s) != 4 || cap(s) != 4) {
		return 0;
	}
	if (ptr(s)[0] != 1) {
		return 0;
	}
	return sum_elems(s);
}

int from_ptr(void) {
	int a[3] = { 0 };
	int[..] s = { 0 };
	a[0] = 10;
	a[1] = 20;
	a[2] = 30;
	s = ranged(a, 3);
	if (len(s) != 3 || cap(s) != 3) {
		return 0;
	}
	return sum_elems(s);
}

int from_scratch(void) {
	int a[4] = { 0 };
	int[..] s = { 0 };
	s = ranged(a, 0, 4);
	if (len(s) != 0 || cap(s) != 4) {
		return 0;
	}
	a[0] = 1;
	a[1] = 2;
	s = ranged(a, 2, 4);
	if (cap(s) != 4 || len(s) != 2) {
		return 0;
	}
	return sum_elems(s);
}

int index_write(void) {
	int a[2] = { 0 };
	int[..] s = { 0 };
	a[0] = 0;
	a[1] = 0;
	s = a;
	s[0] = 7;
	s[1] = 8;
	return a[0] + a[1];
}

int roundtrip(void) {
	int a[2] = { 0 };
	int[..] s = { 0 };
	int[..] t = { 0 };
	a[0] = 4;
	a[1] = 5;
	s = a;
	t = id_ranged(s);
	return t[0] + t[1];
}

int ranged_size(void) {
	return (int) sizeof(int[..]);
}

int from_string(void) {
	char[..] s = { 0 };
	s = "hi";
	if (len(s) != 2 || cap(s) != 2) {
		return 0;
	}
	if (s[0] != 'h' || s[1] != 'i') {
		return 0;
	}
	return 1;
}

int len_fixed(void) {
	int a[5] = { 0 };
	return (int)len(a);
}

int len_ranged(void) {
	int a[3] = { 0 };
	int[..] s = { 0 };
	s = a;
	return (int)len(s);
}

int cap_ranged(void) {
	int a[3] = { 0 };
	int[..] s = { 0 };
	int[..] t = { 0 };
	s = a;
	t = s[0 .. 2];
	if (cap(s) != 3 || cap(t) != 2 || len(t) != 2) {
		return 0;
	}
	return (int)cap(a);
}
