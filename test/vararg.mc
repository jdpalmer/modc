#include "stdarg.h"

int sum(int n, ...) {
	va_list ap = { 0 };
	int s = { 0 };
	int i = { 0 };
	int v = { 0 };
	va_start(ap, n);
	s = 0;
	for (i = 0; i < n; i++) {
		v = va_arg(ap, int);
		s = s + v;
	}
	va_end(ap);
	return s;
}

int sum_copy(int n, ...) {
	va_list ap = { 0 };
	va_list aq = { 0 };
	int s = { 0 };
	int i = { 0 };
	int v = { 0 };
	va_start(ap, n);
	va_copy(aq, ap);
	s = 0;
	for (i = 0; i < n; i++) {
		v = va_arg(aq, int);
		s = s + v;
	}
	va_end(aq);
	va_end(ap);
	return s;
}

int call_sum(void) {
	return sum(3, 10, 20, 12);
}

int add3(int a, int b, int c) {
	return a + b + c;
}

int forward(int n, ...) {
	va_list ap = { 0 };
	int a = { 0 };
	int b = { 0 };
	int c = { 0 };
	va_start(ap, n);
	a = va_arg(ap, int);
	b = va_arg(ap, int);
	c = va_arg(ap, int);
	va_end(ap);
	(void)n;
	return add3(a, b, c);
}
