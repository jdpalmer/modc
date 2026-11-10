/* Struct/union by value: assign, pass, return */

struct Point {
	int x;
	int y;
};

struct Rect {
	Point min;
	Point max;
};

Point id(Point p) {
	return p;
}

int sum(Point p) {
	return p.x + p.y;
}

void set(Point* p, Point v) {
	*p = v;
}

Point add(Point a, Point b) {
	Point r = { a.x + b.x, a.y + b.y };
	return r;
}

Rect idr(Rect r) {
	return r;
}

int local_copy() {
	Point a = { 1, 2 };
	Point b = a;
	return b.x + b.y;
}

int nested() {
	Rect r = { 0 };
	r.min.x = 1;
	r.min.y = 2;
	r.max.x = 3;
	r.max.y = 4;
	Rect s = r;
	return s.min.x + s.max.y;
}

int main() {
	Point a = { 3, 4 };
	if (sum(a) != 7) {
		return 1;
	}
	Point b = id(a);
	if (b.x != 3 || b.y != 4) {
		return 2;
	}
	Point c = add(a, b);
	if (c.x != 6 || c.y != 8) {
		return 3;
	}
	set(&a, c);
	if (a.x != 6 || a.y != 8) {
		return 4;
	}
	if (local_copy() != 3) {
		return 5;
	}
	if (nested() != 5) {
		return 6;
	}
	Rect r = { a, b };
	Rect r2 = idr(r);
	if (r2.min.x != 6 || r2.max.y != 4) {
		return 7;
	}
	return 0;
}
