#include <assert.h>

/* Slice-main sugar: args[0] is the program name; further args are views. */
int main(const char[..][..] args) {
	assert(len(args) >= 1);
	assert(len(args[0]) > 0);
	return 0;
}
