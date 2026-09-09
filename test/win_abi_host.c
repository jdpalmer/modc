/* Compiled with host cc — same layouts as win_abi_modc.mc */
#include "win_abi.h"
#include <stdarg.h>

Pair
host_pair(int a, int b)
{
	Pair p;

	p.a = a;
	p.b = b;
	return p;
}

int
host_pair_sum(Pair p)
{
	return p.a + p.b;
}

Pair
host_pair_id(Pair p)
{
	return p;
}

Quad
host_quad(int a, int b, int c, int d)
{
	Quad q;

	q.a = a;
	q.b = b;
	q.c = c;
	q.d = d;
	return q;
}

int
host_quad_sum(Quad q)
{
	return q.a + q.b + q.c + q.d;
}

Quad
host_quad_id(Quad q)
{
	return q;
}

int
host_vsum(int n, ...)
{
	va_list ap;
	int i, s;

	va_start(ap, n);
	s = 0;
	for (i = 0; i < n; i++)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}
