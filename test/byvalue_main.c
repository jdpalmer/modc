typedef struct Point Point;
typedef struct Rect Rect;

struct Point {
	int x;
	int y;
};

struct Rect {
	Point min;
	Point max;
};

Point id(Point);
int sum(Point);
void set(Point *, Point);
Point add(Point, Point);
Rect idr(Rect);
int local_copy(void);
int nested(void);

int
main(void)
{
	Point a;
	Point b;
	Point c;
	Rect r;
	Rect r2;

	a.x = 3;
	a.y = 4;
	if(sum(a) != 7)
		return 1;
	b = id(a);
	if(b.x != 3 || b.y != 4)
		return 2;
	c = add(a, b);
	if(c.x != 6 || c.y != 8)
		return 3;
	set(&a, c);
	if(a.x != 6 || a.y != 8)
		return 4;
	if(local_copy() != 3)
		return 5;
	if(nested() != 5)
		return 6;
	r.min = a;
	r.max = b;
	r2 = idr(r);
	if(r2.min.x != 6 || r2.max.y != 4)
		return 7;
	return 0;
}
