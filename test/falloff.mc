/* Non-void functions must return on every path. */

enum FallColor {
	FALL_RED, FALL_GREEN, FALL_BLUE };

int simple(void) {
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

int after_loop(void) {
	int i = {0};
	int s = {0};
	s = 0;
	for (i = 0; i < 3; i++) {
		s = s + i;
	}
	return s;
}

void void_ok(void) {
	/* falling off void is fine */
}
