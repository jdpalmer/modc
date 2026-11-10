/* C99 compound literals: (T){…} as rvalues / lvalues. */

typedef struct TagEntry {
	int x;
	int y;
} TagEntry;

typedef struct Pack {
	TagEntry a;
	int n;
} Pack;

int take_entry(TagEntry e) {
	return e.x + e.y;
}

int take_ptr(TagEntry* p) {
	return p.x * 10 + p.y;
}

int main() {
	TagEntry e = (TagEntry){ 0 };
	TagEntry* p = &(TagEntry){ .x = 1, .y = 2 };
	int a0 = ((int[3]){ 9, 8, 7 })[0];
	int a1 = ((int[3]){ 9, 8, 7 })[1];
	int a2 = ((int[3]){ 9, 8, 7 })[2];
	int sum = (int){ 42 };
	Pack pack = (Pack){ .a.x = 1, .a.y = 2, .n = 3 };

	if (e.x != 0 || e.y != 0) {
		return 1;
	}

	e = (TagEntry){ .x = 3, .y = 4 };
	if (e.x != 3 || e.y != 4) {
		return 2;
	}

	if (take_entry((TagEntry){ .x = 5, .y = 7 }) != 12) {
		return 3;
	}

	if (take_ptr(p) != 12) {
		return 4;
	}

	if (a0 != 9 || a1 != 8 || a2 != 7) {
		return 5;
	}

	if (sum != 42) {
		return 6;
	}

	if (pack.a.x != 1 || pack.a.y != 2 || pack.n != 3) {
		return 7;
	}

	e = (TagEntry){ 0 };
	e.x = 11;
	if (e.x != 11 || e.y != 0) {
		return 8;
	}

	return 0;
}
