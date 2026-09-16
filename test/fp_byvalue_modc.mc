/* Compiled with modc — pass/return structs containing double. */
#include "fp_byvalue.h"

Vec2 modc_make(double x, double y) {
	Vec2 v = { 0 };
	v.x = x;
	v.y = y;
	return v;
}

double modc_sum(Vec2 v) {
	return v.x + v.y;
}

Vec2 modc_id(Vec2 v) {
	return v;
}

Mixed modc_mixed(int tag, double val) {
	Mixed m = { 0 };
	m.tag = tag;
	m.val = val;
	return m;
}

int modc_mixed_tag(Mixed m) {
	return m.tag;
}

double modc_mixed_val(Mixed m) {
	return m.val;
}

int modc_calls_host() {
	Vec2 v = { 0 };
	Vec2 w = { 0 };
	Mixed m = { 0 };
	v = host_make(1.0, 2.0);
	if (host_sum(v) != 3.0) {
		return 1;
	}
	w = host_id(v);
	if (w.x != 1.0 || w.y != 2.0) {
		return 2;
	}
	m = host_mixed(7, 3.5);
	if (host_mixed_tag(m) != 7) {
		return 3;
	}
	if (host_mixed_val(m) != 3.5) {
		return 4;
	}
	return 0;
}
