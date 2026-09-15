/* Exhaustive enum switch without default; case ranges cover members. */

enum Color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

int name(Color c) {
	switch (c) {
		case COLOR_RED: return 1;
		case COLOR_GREEN: return 2;
		case COLOR_BLUE: return 3;
	}
}

int range(Color c) {
	switch (c) {
		case COLOR_RED .. COLOR_BLUE: return 10;
	}
}

int with_default(Color c) {
	switch (c) {
		case COLOR_RED: return 1;
		default: return 0;
	}
}

int enum_exhaust_run(void) {
	if (name(COLOR_GREEN) != 2) {
		return 1;
	}
	if (range(COLOR_BLUE) != 10) {
		return 2;
	}
	if (with_default(COLOR_RED) != 1) {
		return 3;
	}
	if (with_default(COLOR_BLUE) != 0) {
		return 4;
	}
	return 0;
}
