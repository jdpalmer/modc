/* Portable test (was uchar.mc + uchar_main.c). */
#include <assert.h>

/* Plain char is unsigned 8-bit. */

int hi_byte_eq() {
	char c = { 0 };
	c = 0xFF;
	return c == 0xFF;
}

int hi_byte_as_int() {
	char c = { 0 };
	c = 0xFF;
	return c;
}

int uchar_alias() {
	char u = { 0 };
	char c = { 0 };
	u = 0xAB;
	c = u;
	return c == 0xAB && u == c;
}

int main() {
	assert(hi_byte_eq());

	assert_eq(hi_byte_as_int(), 255);

	assert(uchar_alias());

	return 0;
}
