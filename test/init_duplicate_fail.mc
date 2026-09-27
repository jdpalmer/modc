// fail: F:subobject is initialized more than once

struct Pair {
	int x;
	int y;
};

struct Pair pair = {
	.x = 1, .x = 2 };
