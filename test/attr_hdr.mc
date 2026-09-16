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
	AttrAligned a = { 0 };
	int(*fp)(int) = { 0 };
	fp = attr_stdcall_fn;
	a.x = fp(7);
	return attr_dllimport_fn() + attr_dllexport_fn(2) + attr_cdecl_fn(3) + a.x;
}
