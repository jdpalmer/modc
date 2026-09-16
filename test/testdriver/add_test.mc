import "testdriver";

#include <assert.h>

int main() {
	assert(add(2, 3) == 5);
	assert_eq(add(1, 1), 2);
	return 0;
}
