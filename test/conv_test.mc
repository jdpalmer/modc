/* Portable test (was conv.mc + conv_main.c). */
#include <assert.h>

#include <stdint.h>

/* Strict conversions: widening and void* stay implicit; narrowing needs cast. */

int narrow_ok(int64_t y) {
	char z = (char)y;
	return z;
}

void* void_ptr_ok(void* p) {
	int* q = p;
	return q;
}

int widen_ok() {
	int n = 3;
	int64_t x = n;
	return (int)x;
}

int malloc_style() {
	int* p = 0;
	return p != 0;
}

int char_lit_ok() {
	char a = 'a';
	char z = '\0';
	char buf[4] = { 0 };
	buf[0] = '\0';
	return (int)a + (int)z + (int)buf[0];
}

int int_lit_fit() {
	short s = 7;
	unsigned short us = 8;
	char b = 97;
	return (int)s + (int)us + (int)b;
}

int main() {
	assert_eq(narrow_ok(7), 7);
	assert_eq(widen_ok(), 3);
	assert_eq(char_lit_ok(), 'a');
	assert_eq(int_lit_fit(), 112);
	return 0;
}
