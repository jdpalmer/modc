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
	Point r = {0};
	r.x = a.x + b.x;
	r.y = a.y + b.y;
	return r;
}

Rect idr(Rect r) {
	return r;
}

int local_copy(void) {
	Point a = {0};
	Point b = {0};
	a.x = 1;
	a.y = 2;
	b = a;
	return b.x + b.y;
}

int nested(void) {
	Rect r = {0};
	Rect s = {0};
	r.min.x = 1;
	r.min.y = 2;
	r.max.x = 3;
	r.max.y = 4;
	s = r;
	return s.min.x + s.max.y;
}
