int sum(int[..] s) {
	int i = { 0 };
	int t = { 0 };
	t = 0;
	for (i = 0; i < (int)len(s); i++) {
		t = t + s[i];
	}
	return t;
}

int conv_array(void) {
	int a[4] = { 0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	return sum(a);
}

int conv_string(void) {
	char[..] s = { 0 };
	s = "ab";
	return (int)len(s);
}
