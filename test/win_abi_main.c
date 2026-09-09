#include "win_abi.h"

Pair modc_pair(int, int);
int modc_pair_sum(Pair);
Pair modc_pair_id(Pair);
Quad modc_quad(int, int, int, int);
int modc_quad_sum(Quad);
Quad modc_quad_id(Quad);
int modc_vsum(int n, ...);
int modc_calls_host(void);

int
main(void)
{
	Pair p;
	Quad q;

	p = modc_pair(10, 32);
	if (modc_pair_sum(p) != 42)
		return 1;
	p = modc_pair_id(p);
	if (p.a != 10 || p.b != 32)
		return 2;
	q = modc_quad(1, 2, 3, 4);
	if (modc_quad_sum(q) != 10)
		return 3;
	q = modc_quad_id(q);
	if (q.a != 1 || q.d != 4)
		return 4;
	if (modc_vsum(4, 1, 2, 3, 4) != 10)
		return 5;
	return modc_calls_host();
}
