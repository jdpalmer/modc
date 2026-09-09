/* Header may use ->; tokens keep this file in span.file. */

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

int
dot_from_hdr(DotPt *p)
{
	return p->x + p->y;
}
