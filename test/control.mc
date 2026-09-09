/* Control flow: if/while/for, comparisons, &&/||, ?: */

int absdiff(int a, int b) {
	if (a >= b) {
		return a - b;
	}
	return b - a;
}

int sum_to(int n) {
	int i = {0};
	int s = {0};
	s = 0;
	for (i = 0; i < n; i++) {
		s = s + i;
	}
	return s;
}

int sum_pair(int n) {
	int i = {0};
	int j = {0};
	int s = {0};
	s = 0;
	for (i = 0, j = 0; i < n; i++, j++) {
		s = s + i + j;
	}
	return s;
}

int fact(int n) {
	int r = {0};
	r = 1;
	while (n > 1) {
		r = r * n;
		n--;
	}
	return r;
}

int countdown(int n) {
	int s = {0};
	s = 0;
	do {
		s = s + n;
		n--;
	}
	while (n > 0);
	return s;
}

int early_break(int n) {
	int i = {0};
	int s = {0};
	s = 0;
	for (i = 0; i < n; i++) {
		if (i == 5) {
			break;
		}
		s = s + i;
	}
	return s;
}

int skip_continue(int n) {
	int i = {0};
	int s = {0};
	s = 0;
	for (i = 0; i < n; i++) {
		if (i == 2) {
			continue;
		}
		s = s + i;
	}
	return s;
}

int land(int a, int b) {
	if (a && b) {
		return 1;
	}
	return 0;
}

int lor(int a, int b) {
	if (a || b) {
		return 1;
	}
	return 0;
}

int cond(int a, int b, int c) {
	return a ? b: c;
}

int max3(int a, int b, int c) {
	int m = {0};
	m = a > b ? a: b;
	return m > c ? m: c;
}
