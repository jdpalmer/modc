enum Color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

int bad(Color c) {
	switch (c) {
		case COLOR_RED: return 1;
		case COLOR_GREEN: return 2;
	}
}
