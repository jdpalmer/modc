/* C99 designated initializers: .field =, [n] = */

struct P {
	int x;
	int y;
};

struct Rect {
	struct P min;
	struct P max;
};

struct P gdot = {
	.y = 20,.x = 10 };

struct Rect grect = {
	.min.x = 1,.min.y = 2,.max.x = 5,.max.y = 6, };

int garr[6] = {
	[0] = 1,[5] = 2 };

int gsparse[5] = {
	[1] = 7,[3] = 9 };

int dot_x() {
	return gdot.x;
}

int dot_y() {
	return gdot.y;
}

int rect_min_x() {
	return grect.min.x;
}

int rect_max_y() {
	return grect.max.y;
}

int arr0() {
	return garr[0];
}

int arr5() {
	return garr[5];
}

int sparse1() {
	return gsparse[1];
}

int sparse3() {
	return gsparse[3];
}
