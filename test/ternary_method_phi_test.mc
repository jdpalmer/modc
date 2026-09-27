/* Portable test (was ternary_method_phi.mc + ternary_method_phi_main.c). */
#include <assert.h>

/* Ternary arm that inlines a method must phi from the arm's exit block. */
typedef struct Buf {
	int n;
}
Buf;

int (Buf* b).line_count_i() {
	if (b.n < 0) {
		return 0;
	}
	return b.n;
}

int ternary_method_phi_run() {
	Buf buf = { 0 };
	int x = 0;
	buf.n = 3;
	x = 1 ? buf.line_count_i() : 0;
	if (x != 3) {
		return 1;
	}
	x = 0 ? buf.line_count_i() : 9;
	if (x != 9) {
		return 2;
	}
	return 0;
}

int main() {
	return ternary_method_phi_run();
}
