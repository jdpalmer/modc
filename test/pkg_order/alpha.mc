/* Sorts before point.mc — uses Point before its defining file is scanned. */
typedef struct Point Point;
struct Outer {
	Point p;
};

int (Point* p).sum(void) {
	return p.x + p.y;
}

int outer_sum(Outer* o) {
	return o.p.x + o.p.y;
}
