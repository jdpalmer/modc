/* ok: if (!retains_call(p)) discharges ownership on the call */

typedef struct Box {
	int x;
} Box;

void stash([[retains]] Box* p) {
	(void)p;
}

bool take_box([[retains]] Box* p) {
	stash(p);
	return true;
}

bool take_box_move(Box b) {
	(void)b;
	return true;
}

int retains_in_if() {
	Box* p = alloc(Box);

	if (!take_box(p)) {
		return 1;
	}
	return 0;
}

int retains_in_if_pos() {
	Box* p = alloc(Box);

	if (take_box(p)) {
		return 0;
	}
	return 1;
}

int separate_bool_still_ok() {
	Box* p = alloc(Box);
	bool ok = take_box(p);

	if (!ok) {
		return 1;
	}
	return 0;
}

int by_value_in_if() {
	Box b = { .x = 1 };

	if (!take_box_move(b)) {
		return 1;
	}
	return 0;
}

int main() {
	return 0;
}
