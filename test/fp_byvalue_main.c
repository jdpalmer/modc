#include "fp_byvalue.h"

Vec2 modc_make(double, double);
double modc_sum(Vec2);
Vec2 modc_id(Vec2);
Mixed modc_mixed(int, double);
int modc_mixed_tag(Mixed);
double modc_mixed_val(Mixed);
int modc_calls_host(void);

static int
eq(double a, double b)
{
	return a == b;
}

int
main(void)
{
	Vec2 v;
	Vec2 w;
	Mixed m;

	v = modc_make(10.0, 32.0);
	if(!eq(modc_sum(v), 42.0))
		return 1;
	w = modc_id(v);
	if(!eq(w.x, 10.0) || !eq(w.y, 32.0))
		return 2;
	m = modc_mixed(99, 0.25);
	if(modc_mixed_tag(m) != 99)
		return 3;
	if(!eq(modc_mixed_val(m), 0.25))
		return 4;
	return modc_calls_host();
}
