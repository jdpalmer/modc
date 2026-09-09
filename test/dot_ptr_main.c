typedef struct DotPt DotPt;
typedef struct DotInner DotInner;
typedef struct DotOuter DotOuter;

struct DotPt {
	int x;
	int y;
};

struct DotInner {
	DotPt p;
};

struct DotOuter {
	DotInner *inner;
};

int dot_from_hdr(DotPt *);
int dot_user(DotPt *);
int dot_set(DotPt *, int, int);
int dot_nested(DotOuter *);

int
main(void)
{
	DotPt p;
	DotInner in;
	DotOuter o;

	if(dot_set(&p, 10, 32) != 42)
		return 1;
	if(dot_user(&p) != 42)
		return 2;
	if(dot_from_hdr(&p) != 42)
		return 3;
	in.p.x = 3;
	in.p.y = 4;
	o.inner = &in;
	if(dot_nested(&o) != 7)
		return 4;
	return 0;
}
