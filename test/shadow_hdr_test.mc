/* Portable test (was shadow_hdr.mc + shadow_hdr_main.c). */
#include <assert.h>

#include "shadow_hdr.h"

int from_hdr(int x) {
	return shadow_ok(x);
}

int main() {
 assert_eq(from_hdr(2), 1);
	return 0; 
}
