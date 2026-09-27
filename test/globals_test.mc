/* Portable test (was globals.mc + globals_main.c). */
#include <assert.h>

/* File-scope globals and string literals */

int g;
int gi = 7;
static int s;
int ga[3] = { 1, 2, 3 };
int* gp = &g;
int* gap = ga + 1;
int* gai = &ga[2];
float gf = 1;
double gd = 1.5;
double ge = 1.0 + 2.5;

int twice(int x) {
	return x * 2;
}

int(*gfp)(int) = twice;

int get_g() {
	return g;
}

void set_g(int v) {
	g = v;
}

int get_gi() {
	return gi;
}

int bump_s() {
	s = s + 1;
	return s;
}

int global_ptrs() {
	return gp == &g && gap == &ga[1] && *gap == 2 && gai == &ga[2] &&
	       *gai == 3 && gfp(21) == 42;
}

int global_floats() {
	return gf == 1.0f && gd == 1.5 && ge == 3.5;
}

int first_char() {
	const char *p = { 0 };
	p = "hi";
	return p[0];
}

int str_len3() {
	const char *p = { 0 };
	p = "abc";
	return p[0] + p[1] + p[2];
}

int main() {
	assert_eq(get_gi(), 7);

	assert_eq(get_g(), 0);

	set_g(42);
	assert_eq(get_g(), 42);

	assert_eq(bump_s(), 1);

	assert_eq(bump_s(), 2);

	assert(global_ptrs());

	assert(global_floats());

	assert_eq(first_char(), 'h');

	assert_eq(str_len3(), 'a' + 'b' + 'c');

	return 0;
}
