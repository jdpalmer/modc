struct Widget {
	int id_val;
};

struct Window {
	Widget;
	int title_len_val;
};

int (Window* w).title_len(void) {
	return w.title_len_val;
}

void (Window* w).set_title_len(int n) {
	w.title_len_val = n;
}

int (Widget* w).id(void) {
	return w.id_val;
}

int test_method(void) {
	Window win = {0};
	win.title_len_val = 3;
	win.id_val = 7;
	if (win.title_len() != 3) {
		return 1;
	}
	win.set_title_len(9);
	if (win.title_len() != 9) {
		return 2;
	}
	if (win.id() != 7) {
		return 3;
	}
	return 0;
}
