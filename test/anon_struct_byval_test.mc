typedef struct {
	int x;
	int y;
} Point;
int add_pt(Point p) { return p.x + p.y; }
int main() {
	Point p = { .x = 2, .y = 3 };
	return add_pt(p) == 5 ? 0 : 1;
}
