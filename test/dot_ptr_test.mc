/* User TUs: '.' through pointers; nested pointer fields. */

#include "dot_ptr.h"

int dot_user(DotPt* p) {
	return p.x + p.y;
}

int dot_set(DotPt* p, int x, int y) {
	p.x = x;
	p.y = y;
	return p.x + p.y;
}

int dot_nested(DotOuter* o) {
	return o.inner.p.x + o.inner.p.y;
}

int main() {
	DotPt p = { 0 };
	DotInner in = { 0 };
	DotOuter o = { 0 };

	if (dot_set(&p, 10, 32) != 42) {
		return 1;
	}
	if (dot_user(&p) != 42) {
		return 2;
	}
	if (dot_from_hdr(&p) != 42) {
		return 3;
	}
	in.p.x = 3;
	in.p.y = 4;
	o.inner = &in;
	if (dot_nested(&o) != 7) {
		return 4;
	}
	return 0;
}
