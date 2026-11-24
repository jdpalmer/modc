/* (T[N]){…} and (T[..]){…} compound literals at ranged sinks. */

int sum(int[..] s) {
	int t = 0;

	for (auto x : s) {
		t = t + x;
	}
	return t;
}

int main() {
	int[..] a = (int[3]){1, 2, 3};
	int[..] b = (int[..]){4, 5, 6};

	if (sum((int[3]){1, 2, 3}) != 6) {
		return 1;
	}
	if (sum((int[..]){10, 20}) != 30) {
		return 2;
	}
	if (sum(a) != 6) {
		return 3;
	}
	if (sum(b) != 15) {
		return 4;
	}
	if (len(a) != 3 || cap(a) != 3) {
		return 5;
	}
	if (len(b) != 3 || cap(b) != 3) {
		return 6;
	}
	return 0;
}
