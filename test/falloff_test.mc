/* Portable test (was falloff.mc + falloff_main.c). */
#include <assert.h>

/* Non-void functions must return on every path. */

enum FallColor {
	FALL_RED,
	FALL_GREEN,
	FALL_BLUE
};

int simple() {
	return 42;
}

int both(int c) {
	if (c) {
		return 1;
	} else {
		return 0;
	}
}

int via_switch(int x) {
	switch (x) {
		case 0: return 10;
		case 1: return 20;
		default: return 30;
	}
}

int via_enum(FallColor c) {
	switch (c) {
		case FALL_RED: return 1;
		case FALL_GREEN: return 2;
		case FALL_BLUE: return 3;
	}
}

int after_loop() {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	for (i = 0; i < 3; i++) {
		s = s + i;
	}
	return s;
}

void void_ok() {
	/* falling off void is fine */
}

int main() {
	assert_eq(simple(), 42);

	if (both(1) != 1 || both(0) != 0) {

		return 2;

	}

	if (via_switch(0) != 10 || via_switch(1) != 20 || via_switch(2) != 30) {

		return 3;

	}

	if (via_enum((FallColor)0) != 1 || via_enum((FallColor)1) != 2 || via_enum((FallColor)2) != 3) {

		return 5;

	}

	assert_eq(after_loop(), 3);

	void_ok();
	return 0;
}
