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
	COLOR_RED, COLOR_GREEN, COLOR_BLUE };

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
	return (int)c;
}

int compat_struct(struct Point sp) {
	return sp.x + sp.y;
}

int compat_union(union U u) {
	return u.i;
}

int compat_enum(enum Color c) {
	return (int)c;
}

int typedef_ok(void) {
	typedef struct Point Point;
	Point p = {0};
	p.x = 1;
	p.y = 2;
	return p.x + p.y;
}
