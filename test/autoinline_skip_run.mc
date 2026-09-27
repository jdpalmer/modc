/* Auto-inline: bodies with defer / goto stay as call. */
static int with_goto(int x) {
	if (x > 0) {
		goto done;
	}
	return -1;
	done: return x;
}

static int with_defer(int x) {
	defer(void)x;
	return x;
}

int autoinline_skip_run() {
	if (with_goto(3) != 3) {
		return 1;
	}
	if (with_defer(4) != 4) {
		return 2;
	}
	return 0;
}
