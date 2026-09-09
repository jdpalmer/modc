typedef struct Point Point;
typedef struct Rect Rect;
union U;
enum Color;

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

int point_sum(Point);
int rect_w(Rect);
int union_i(union U);
int color_val(enum Color);
int compat_struct(struct Point);
int compat_union(union U);
int compat_enum(enum Color);
int typedef_ok(void);

int
main(void)
{
	Point p;
	Rect r;
	union U u;
	enum Color c;

	p.x = 3;
	p.y = 4;
	if(point_sum(p) != 7)
		return 1;
	r.min.x = 1;
	r.max.x = 5;
	if(rect_w(r) != 4)
		return 2;
	u.i = 42;
	if(union_i(u) != 42)
		return 3;
	c = COLOR_GREEN;
	if(color_val(c) != 1)
		return 4;
	if(compat_struct(p) != 7)
		return 5;
	if(compat_union(u) != 42)
		return 6;
	if(compat_enum(COLOR_BLUE) != 2)
		return 7;
	if(typedef_ok() != 3)
		return 8;
	return 0;
}
