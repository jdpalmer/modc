// fail: F:initializer of 'x' is never used

int both_assign(int c) {
	int x = 0;
	if (c) {
		x = 1;
	} else {
		x = 2;
	}
	return x;
}

int straight() {
	int y = 0;
	y = 1;
	return y;
}

struct Point {
	int x;
	int y;
};

int aggr_both(int c) {
	Point p = { 0 };
	Point a = { 1, 2 };
	Point b = { 3, 4 };
	if (c) {
		p = a;
	} else {
		p = b;
	}
	return p.x;
}

int aggr_straight() {
	Point q = { 0 };
	Point r = { 5, 6 };
	q = r;
	return q.x;
}

/* Every named field overwritten before any read of remaining init. */
int field_both() {
	Point p = { 0 };
	p.x = 3;
	p.y = 4;
	return p.x + p.y;
}

int field_both_branch(int c) {
	Point p = { 0 };
	if (c) {
		p.x = 1;
		p.y = 2;
	} else {
		p.x = 3;
		p.y = 4;
	}
	return p.x;
}

int ranged_rebind() {
	int a[2] = { 0 };
	int[..] s = { 0 };
	s = ranged(a, 2);
	return s[0];
}

int main() {
	return 0;
}
