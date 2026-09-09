/* Compiled with host cc — same struct layout as fp_byvalue_modc.c */
#include "fp_byvalue.h"

Vec2
host_make(double x, double y)
{
	Vec2 v;

	v.x = x;
	v.y = y;
	return v;
}

double
host_sum(Vec2 v)
{
	return v.x + v.y;
}

Vec2
host_id(Vec2 v)
{
	return v;
}

Mixed
host_mixed(int tag, double val)
{
	Mixed m;

	m.tag = tag;
	m.val = val;
	return m;
}

int
host_mixed_tag(Mixed m)
{
	return m.tag;
}

double
host_mixed_val(Mixed m)
{
	return m.val;
}
