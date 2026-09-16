/* Forward goto that does not skip a declaration. */

int forward_ok(int x) {
	if (x < 0) {
		goto neg;
	}
	return x;
	neg: return -1;
}

int back_ok(int n) {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	i = 0;
	again: if (i >= n) {
		goto done;
	}
	s = s + i;
	i++;
	goto again;
	done: return s;
}

int decl_before_goto() {
	int x = { 0 };
	x = 3;
	goto out;
	out: return x;
}
