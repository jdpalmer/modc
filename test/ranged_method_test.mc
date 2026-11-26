/* T[..] / T[..]* receiver methods. */

int (int[..] a).sum() {
	int t = 0;
	for (int i = 0; i < (int)len(a); i++) {
		t += a[i];
	}
	return t;
}

bool (int[..]* a).set0(int v) {
	if (len(*a) < 1) {
		return false;
	}
	(*a)[0] = v;
	return true;
}

int main() {
	int buf[3] = { 1, 2, 3 };
	int[..] s = buf;
	int[..]* p = &s;

	if (s.sum() != 6) {
		return 1;
	}
	if (!p.set0(9)) {
		return 2;
	}
	if (s[0] != 9 || s.sum() != 14) {
		return 3;
	}
	return 0;
}
