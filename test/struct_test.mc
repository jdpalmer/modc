/* Structs, arrays, and pointer arithmetic */

struct Point {
	int x;
	int y;
};

struct Rect {
	Point min;
	Point max;
};

int sumpt(Point* p) {
	return p.x + p.y;
}

int setpt(Point* p, int x, int y) {
	p.x = x;
	p.y = y;
	return p.x + p.y;
}

int localpt() {
	Point p = { 0 };
	p.x = 3;
	p.y = 4;
	return p.x + p.y;
}

int sumarr(int* a, int n) {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	for (i = 0; i < n; i++) {
		s = s + a[i];
	}
	return s;
}

int sumlocal() {
	int a[4] = { 0 };
	int i = { 0 };
	int s = { 0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	s = 0;
	for (i = 0; i < 4; i++) {
		s = s + a[i];
	}
	return s;
}

int ptrwalk(int* p, int n) {
	int s = { 0 };
	s = 0;
	while (n > 0) {
		s = s +*p;
		p++;
		n--;
	}
	return s;
}

int rect_area(Rect* r) {
	int w = { 0 };
	int h = { 0 };
	w = r.max.x - r.min.x;
	h = r.max.y - r.min.y;
	return w * h;
}

int main() {
	Point p = { 0 };
	Rect r = { 0 };
	int a[4] = { 0 };

	if (localpt() != 7) {
		return 1;
	}
	if (setpt(&p, 10, 32) != 42) {
		return 2;
	}
	if (sumpt(&p) != 42) {
		return 3;
	}
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	if (sumarr(a, 4) != 10) {
		return 4;
	}
	if (sumlocal() != 10) {
		return 5;
	}
	if (ptrwalk(a, 4) != 10) {
		return 6;
	}
	r.min.x = 1;
	r.min.y = 2;
	r.max.x = 5;
	r.max.y = 6;
	if (rect_area(&r) != 16) {
		return 7;
	}
	return 0;
}
