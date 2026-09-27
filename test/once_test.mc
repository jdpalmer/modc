/* Portable test (was once.mc + once_main.c). */
#include <assert.h>

#include "once_guard.h"
#include "once_guard.h"

int once_value() {
	return ONCE_VALUE;
}

int main() {
	assert_eq(once_value(), 99);
	return 0;
}
