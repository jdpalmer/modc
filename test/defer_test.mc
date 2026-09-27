/* Block-scoped defer: LIFO cleanup at block exit, return, break, goto. */

int defer_log[16];
int nlog;

void tr(int v) {
	if (nlog < 16) {
		defer_log[nlog++] = v;
	}
}

int lifo() {
	tr(0);
	defer tr(3);
	defer tr(2);
	tr(1);
	return 0;
}

int on_return() {
	defer tr(100);
	return 42;
}

int on_block_end() {
	tr(0);
	{
		defer tr(2);
		defer tr(1);
		tr(10);
	}
	tr(20);
	return 0;
}

int on_break() {
	nlog = 0;
	while (1) {
		defer tr(1);
		break;
	}
	return 0;
}

int on_goto() {
	nlog = 0;
	{
		defer tr(5);
		goto out;
	}
	out: return 0;
}

int goto_same_scope() {
	nlog = 0;
	defer tr(6);
	goto same;
	same: tr(7);
	return 0;
}

int on_branch(int x) {
	defer tr(9);
	if (x) {
		defer tr(8);
		return 1;
	}
	return 2;
}

int with_ranged() {
	int a[3] = { 0 };
	int[..] s = { 0 };
	nlog = 0;
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	s = a;
	defer tr(99);
	return s[0] + s[1] + s[2];
}

/* Return expression runs before defers (temps, then LIFO cleanup, then ret). */
int ret_order_stamp;

int ret_order_mark(int v) {
	ret_order_stamp = v;
	return v;
}

int ret_order() {
	defer ret_order_mark(2);
	return ret_order_mark(1);
}

(int, int) ret_order_tuple() {
	defer ret_order_mark(20);
	return (ret_order_mark(10), 7);
}

int ret_order_tuple_ok() {
	ret_order_stamp = 0;
	{
		auto (a, b) = ret_order_tuple();
		if (a != 10 || b != 7) {
			return 1;
		}
		if (ret_order_stamp != 20) {
			return 2;
		}
	}
	return 0;
}

struct ReturnPair {
	int x;
	int y;
};

struct ReturnPair aggregate_return_snapshot() {
	struct ReturnPair p = {
		10, 11 };
	defer {
		p.x = 90;
		p.y = 91;
	}
	return p;
}

int main() {
	nlog = 0;
	if (lifo() != 0) {
		return 1;
	}
	if (nlog != 4 || defer_log[0] != 0 || defer_log[1] != 1 || defer_log[2] != 2 || defer_log[3] != 3) {
		return 2;
	}

	nlog = 0;
	if (on_return() != 42) {
		return 3;
	}
	if (nlog != 1 || defer_log[0] != 100) {
		return 4;
	}

	nlog = 0;
	if (on_block_end() != 0) {
		return 5;
	}
	if (nlog != 5 || defer_log[0] != 0 || defer_log[1] != 10 || defer_log[2] != 1 || defer_log[3] != 2 || defer_log[4] != 20) {
		return 6;
	}

	if (on_break() != 0) {
		return 7;
	}
	if (nlog != 1 || defer_log[0] != 1) {
		return 8;
	}

	if (on_goto() != 0) {
		return 9;
	}
	if (nlog != 1 || defer_log[0] != 5) {
		return 10;
	}

	if (goto_same_scope() != 0) {
		return 20;
	}
	if (nlog != 2 || defer_log[0] != 7 || defer_log[1] != 6) {
		return 21;
	}

	nlog = 0;
	if (on_branch(1) != 1) {
		return 11;
	}
	if (nlog != 2 || defer_log[0] != 8 || defer_log[1] != 9) {
		return 12;
	}

	nlog = 0;
	if (on_branch(0) != 2) {
		return 13;
	}
	if (nlog != 1 || defer_log[0] != 9) {
		return 14;
	}

	if (with_ranged() != 6) {
		return 15;
	}
	if (nlog != 1 || defer_log[0] != 99) {
		return 16;
	}

	ret_order_stamp = 0;
	if (ret_order() != 1) {
		return 17;
	}
	if (ret_order_stamp != 2) {
		return 18;
	}

	if (ret_order_tuple_ok() != 0) {
		return 19;
	}

	{
		struct ReturnPair p = aggregate_return_snapshot();
		if (p.x != 10 || p.y != 11) {
			return 22;
		}
	}

	return 0;
}
