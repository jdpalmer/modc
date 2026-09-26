/* Curated <math.h> stub + host libm (not Apple SDK math.h / _Float16). */
#pragma modc c_libs(m)

#include <math.h>

int math_hdr_run() {
	double a = { 0 };
	float b = { 0 };
	a = sqrt(4.0);
	if (a != 2.0) {
		return 1;
	}
	if (fabs(-3.0) != 3.0) {
		return 2;
	}
	b = sqrtf(9.0f);
	if (b != 9.0f / 3.0f) {
		return 3;
	}
	if (floor(3.7) != 3.0) {
		return 4;
	}
	if (ceil(3.2) != 4.0) {
		return 5;
	}
	if (pow(2.0, 3.0) != 8.0) {
		return 6;
	}
	if (sin(0.0) != 0.0) {
		return 7;
	}
	if (cos(0.0) != 1.0) {
		return 8;
	}
	if (M_PI < 3.14 || M_PI > 3.15) {
		return 9;
	}
	if (!isfinite(1.0) || !isfinite(0.0)) {
		return 10;
	}
	if (isfinite(INFINITY) || isfinite(NAN)) {
		return 11;
	}
	if (!isinf(INFINITY) || isinf(1.0)) {
		return 12;
	}
	if (!isnan(NAN) || isnan(0.0)) {
		return 13;
	}
	return 0;
}
