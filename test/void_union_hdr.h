/* Headers may mix pointers with non-pointers, overlay structs, and use void* arithmetic. */

struct HdrPt {
	int x;
	int y;
};

union HdrSlot {
	void *p;
	int x;
	struct HdrPt pt;
};

static int
hdr_void_arith(void *p)
{
	void *q;

	q = p + 1;
	(void)q;
	return 1;
}

static int
hdr_union_ok(void)
{
	union HdrSlot u;

	u.x = 1;
	return u.x;
}
