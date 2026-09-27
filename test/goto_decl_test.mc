/* Portable test (was goto_decl.mc + goto_decl_main.c). */
#include <assert.h>

/* Forward goto that does not skip a declaration. */

int forward_ok(int x) {
	if (x < 0) {
		goto neg;
	}
	return x;
	neg: return -1;
}

int back_ok(int n) {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	i = 0;
	again: if (i >= n) {
		goto done;
	}
	s = s + i;
	i++;
	goto again;
	done: return s;
}

int decl_before_goto() {
	int x = { 0 };
	x = 3;
	goto out;
	out: return x;
}

int label_one() {
	goto done;
	done: return 1;
}

int label_two() {
	goto done;
	done: return 2;
}

int label_namespace() {
	int done = 3;
	goto done;
	done: return done;
}

int main() {
	if (forward_ok(2) != 2 || forward_ok(-1) != -1) {

		return 1;

	}

	assert_eq(back_ok(4), 6);

	assert_eq(decl_before_goto(), 3);

	if (label_one() != 1 || label_two() != 2) {

		return 4;

	}

	assert_eq(label_namespace(), 3);

	return 0;
}
