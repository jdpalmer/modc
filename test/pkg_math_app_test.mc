/* Portable test (was pkg_math_app.mc + pkg_math_app_main.c). */
#include <assert.h>

import "pkg_math";

float pkg_math_sum() {
	Vec2 v = { 0 };
	v = vec2(3, 4);
	return vec2_len2(v);
}

int main() {
	assert_eq(pkg_math_sum(), 25);
	return 0;
}
