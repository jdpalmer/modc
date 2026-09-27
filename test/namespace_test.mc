/* Unified struct/union/enum tag namespace — no typedef required. */

struct Point {
	int x;
	int y;
};

struct Rect {
	Point min;
	Point max;
};

union U {
	int i;
	Point p;
};

enum Color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

int point_sum(Point v) {
	return v.x + v.y;
}

int rect_w(Rect r) {
	return r.max.x - r.min.x;
}

int union_i(union U u) {
	return u.i;
}

int color_val(Color c) {
	return c;
}

int compat_struct(struct Point sp) {
	return sp.x + sp.y;
}

int compat_union(union U u) {
	return u.i;
}

int compat_enum(enum Color c) {
	return c;
}

int typedef_ok() {
	typedef struct Point Point;
	Point p = { 0 };
	p.x = 1;
	p.y = 2;
	return p.x + p.y;
}

int main() {
	Point p = { 0 };
	Rect r = { 0 };
	union U u = { 0 };
	Color c = COLOR_RED;

	p.x = 3;
	p.y = 4;
	if (point_sum(p) != 7) {
		return 1;
	}
	r.min.x = 1;
	r.max.x = 5;
	if (rect_w(r) != 4) {
		return 2;
	}
	u.i = 42;
	if (union_i(u) != 42) {
		return 3;
	}
	c = COLOR_GREEN;
	if (color_val(c) != 1) {
		return 4;
	}
	if (compat_struct(p) != 7) {
		return 5;
	}
	if (compat_union(u) != 42) {
		return 6;
	}
	if (compat_enum(COLOR_BLUE) != 2) {
		return 7;
	}
	if (typedef_ok() != 3) {
		return 8;
	}
	return 0;
}
