#include <stdint.h>

/* Strict conversions: widening and void* stay implicit; narrowing needs cast. */

int narrow_ok(int64_t y) {
	char z = { 0 };
	z = (char)y;
	return z;
}

void* void_ptr_ok(void* p) {
	int* q = { 0 };
	q = p;
	return q;
}

int widen_ok(void) {
	int64_t x = { 0 };
	int n = { 0 };
	n = 3;
	x = n;
	return (int)x;
}

int malloc_style(void) {
	int* p = { 0 };
	p = 0;
	return p != 0;
}

int char_lit_ok(void) {
	char a = { 0 };
	char z = { 0 };
	char buf[4] = { 0 };
	a = 'a';
	z = '\0';
	buf[0] = '\0';
	return (int)a + (int)z + (int)buf[0];
}

int int_lit_fit(void) {
	short s = { 0 };
	unsigned short us = { 0 };
	char b = { 0 };
	s = 7;
	us = 8;
	b = 97;
	return (int)s + (int)us + (int)b;
}
