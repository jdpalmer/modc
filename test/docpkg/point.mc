// Point is a 2D coordinate.
struct Point {
	int x;
	int y;
};

enum Color {
	ColorRed = 1,
	ColorBlue = 2
};

enum {
	DocPkgMax = 8
};

// origin reports whether p is at (0, 0).
bool (Point *p).origin() {
	return p.x == 0 && p.y == 0;
}

// coords returns p's x and y.
(int, int) (Point *p).coords() {
	return (p.x, p.y);
}
