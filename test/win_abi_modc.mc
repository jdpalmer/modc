/* Compiled with modc — integer struct return + varargs vs host C (Win ABI). */
#include "win_abi.h"
#include "stdarg.h"

Pair modc_pair(int a, int b) {
	Pair p = {0};
	p.a = a;
	p.b = b;
	return p;
}

int modc_pair_sum(Pair p) {
	return p.a + p.b;
}

Pair modc_pair_id(Pair p) {
	return p;
}

Quad modc_quad(int a, int b, int c, int d) {
	Quad q = {0};
	q.a = a;
	q.b = b;
	q.c = c;
	q.d = d;
	return q;
}

int modc_quad_sum(Quad q) {
	return q.a + q.b + q.c + q.d;
}

Quad modc_quad_id(Quad q) {
	return q;
}

int modc_vsum(int n, ...) {
	va_list ap = {0};
	int i = {0};
	int s = {0};
	int v = {0};

	va_start(ap, n);
	s = 0;
	for (i = 0; i < n; i++) {
		v = va_arg(ap, int);
		s = s + v;
	}
	va_end(ap);
	return s;
}

int modc_calls_host(void) {
	Pair p = {0};
	Quad q = {0};

	p = host_pair(3, 4);
	if (host_pair_sum(p) != 7) {
		return 1;
	}
	p = host_pair_id(p);
	if (p.a != 3 || p.b != 4) {
		return 2;
	}
	q = host_quad(1, 2, 3, 4);
	if (host_quad_sum(q) != 10) {
		return 3;
	}
	q = host_quad_id(q);
	if (q.a != 1 || q.d != 4) {
		return 4;
	}
	if (host_vsum(3, 10, 20, 30) != 60) {
		return 5;
	}
	return 0;
}
