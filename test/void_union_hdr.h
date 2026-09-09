/* Headers may mix pointers with non-pointers in unions, and use void* arithmetic. */

union HdrSlot {
	void *p;
	int x;
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
