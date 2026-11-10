/* Portable test (was attr_hdr.mc + attr_hdr_main.c). */
#include <assert.h>

#include "attr_hdr.h"

int attr_dllimport_fn() {
	return 1;
}

int attr_dllexport_fn(int x) {
	return x + 1;
}

int attr_stdcall_fn(int x) {
	return x * 2;
}

int attr_cdecl_fn(int x) {
	return x * 3;
}

void attr_gnu_noreturn() {
}

int use_attrs() {
	int(*fp)(int) = attr_stdcall_fn;
	AttrAligned a = { fp(7) };
	return attr_dllimport_fn() + attr_dllexport_fn(2) + attr_cdecl_fn(3) + a.x;
}

int main() {
	assert_eq(use_attrs(), 1 + 3 + 9 + 14);
	return 0;
}
