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
