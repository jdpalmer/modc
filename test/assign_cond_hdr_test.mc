/* Portable test (was assign_cond_hdr.mc + assign_cond_hdr_main.c). */
#include <assert.h>

#include "assign_cond.h"

int from_hdr() {
	return hdr_assign_cond(1) == 0;
}

int main() {
	assert(from_hdr());

	return 0;
}
