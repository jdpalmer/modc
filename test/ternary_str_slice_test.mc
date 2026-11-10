/* Runtime: ternary of string literals → const char[..] */

bool same(const char[..] a, const char[..] b) {
	int n = (int)len(a);

	if (n != (int)len(b)) {
		return false;
	}
	for (int i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

const char[..] label(int ok) {
	return ok ? "ok" : "err";
}

int main() {
	int which = 1;
	const char[..] s = which ? "yes" : "no";
	const char[..] t = which ? s : "no";

	if (!same(s, "yes")) {
		return 1;
	}
	if (!same(t, "yes")) {
		return 2;
	}
	if (!same(label(1), "ok")) {
		return 3;
	}
	if (!same(label(0), "err")) {
		return 4;
	}
	which = 0;
	s = which ? "yes" : "no";
	if (!same(s, "no")) {
		return 5;
	}
	return 0;
}
