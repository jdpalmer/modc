/* Portable test (was ternary_wide.mc + ternary_wide_main.c). */
#include <assert.h>

/* Ternary with mixed int/int64 widths must not emit coerce before phi. */
#include <stdint.h>

int64_t pick(int c, int64_t x) {
	return c ? x: 0;
}

int ternary_wide_run() {
	int x = { 1 };
	int c = { 1 };
	if ((int)pick(1, 5) != 5) {
		return 1;
	}
	if ((int)pick(0, 5) != 0) {
		return 2;
	}
	if ((c ? x: 0) != 1) {
		return 3;
	}
	return 0;
}

int main() {
	return ternary_wide_run();
}
