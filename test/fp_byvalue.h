/* Shared layout for modc ↔ host cc struct-by-value interop tests. */
#ifndef FP_BYVALUE_H
#define FP_BYVALUE_H

typedef struct Vec2 Vec2;
struct Vec2 {
	double x;
	double y;
};

typedef struct Mixed Mixed;
struct Mixed {
	int tag;
	double val;
};

Vec2 host_make(double x, double y);
double host_sum(Vec2 v);
Vec2 host_id(Vec2 v);
Mixed host_mixed(int tag, double val);
int host_mixed_tag(Mixed m);
double host_mixed_val(Mixed m);

#endif
