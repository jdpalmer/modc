// fail: F:%C uses '.' for field access; '->' is for headers

struct Point {
	int x;
	int y;
};

int bad(Point* p) {
	return p->x;
}
