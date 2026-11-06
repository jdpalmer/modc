int partial(int c) {
	int x = 0;
	if (c) {
		x = 1;
	}
	return x;
}

int used() {
	int x = 3;
	return x;
}

int reassign_after_read(int c) {
	int x = 0;
	if (c) {
		return x;
	}
	x = 1;
	return x;
}

int addr_escape() {
	int x = 0;
	int* p = &x;
	*p = 9;
	return x;
}

struct Point {
	int x;
	int y;
};

/* Field stores do not kill the whole-object initializer. */
int field_writes() {
	Point p = { 0 };
	p.x = 3;
	p.y = 4;
	return p.x + p.y;
}

/* Taking &p.x observes the object (same as &p). */
int field_addr() {
	Point p = { 0 };
	int* px = &p.x;
	*px = 9;
	return p.x;
}

int aggr_partial(int c) {
	Point p = { 0 };
	Point o = { 1, 2 };
	if (c) {
		p = o;
	}
	return p.x;
}

int ranged_index() {
	int a[2] = { 7, 8 };
	int[..] s = ranged(a, 2);
	s[0] = 1;
	return s[0];
}

int main() {
	(void)partial(0);
	(void)used();
	(void)reassign_after_read(1);
	(void)addr_escape();
	(void)field_writes();
	(void)field_addr();
	(void)aggr_partial(0);
	(void)ranged_index();
	return 0;
}
