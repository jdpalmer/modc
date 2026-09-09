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

int sumpt(Point *);
int setpt(Point *, int, int);
int localpt(void);
int sumarr(int *, int);
int sumlocal(void);
int ptrwalk(int *, int);
int rect_area(Rect *);

int
main(void)
{
	Point p;
	Rect r;
	int a[4];

	if(localpt() != 7)
		return 1;
	if(setpt(&p, 10, 32) != 42)
		return 2;
	if(sumpt(&p) != 42)
		return 3;
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	if(sumarr(a, 4) != 10)
		return 4;
	if(sumlocal() != 10)
		return 5;
	if(ptrwalk(a, 4) != 10)
		return 6;
	r.min.x = 1;
	r.min.y = 2;
	r.max.x = 5;
	r.max.y = 6;
	if(rect_area(&r) != 16)
		return 7;
	return 0;
}
