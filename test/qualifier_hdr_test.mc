/* Portable test (was qualifier_hdr.mc + qualifier_hdr_main.c). */
#include <assert.h>

#include "qualifier_hdr.h"

int qualifier_hdr_run() {
	return hdr_qual_ok();
}

int main() {
	assert_eq(qualifier_hdr_run(), 5);
	return 0;
}
