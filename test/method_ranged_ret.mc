/* Methods may return ranged slices: T[..] (Recv *r).name(...). */
typedef struct Line {
	char buf[8];
	int n;
}
Line;

char[..] (Line* l).view(void) {
	return ranged(l.buf, l.n);
}

int method_ranged_ret_run(void) {
	Line line = { 0 };
	char[..] v = { 0 };
	line.buf[0] = 'a';
	line.buf[1] = 'b';
	line.n = 2;
	v = line.view();
	if (len(v) != 2) {
		return 1;
	}
	if (v[0] != 'a' || v[1] != 'b') {
		return 2;
	}
	return 0;
}
