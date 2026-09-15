/* Block-scoped defer: LIFO cleanup at block exit, return, break, goto. */

int defer_log[16];
int nlog;

void tr(int v) {
	if (nlog < 16) {
		defer_log[nlog++] = v;
	}
}

int lifo(void) {
	tr(0);
	defer tr(3);
	defer tr(2);
	tr(1);
	return 0;
}

int on_return(void) {
	defer tr(100);
	return 42;
}

int on_block_end(void) {
	tr(0);
	{
		defer tr(2);
		defer tr(1);
		tr(10);
	}
	tr(20);
	return 0;
}

int on_break(void) {
	nlog = 0;
	while (1) {
		defer tr(1);
		break;
	}
	return 0;
}

int on_goto(void) {
	nlog = 0;
	{
		defer tr(5);
		goto out;
	}
	out: return 0;
}

int with_ranged(void) {
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

int ret_order(void) {
	defer ret_order_mark(2);
	return ret_order_mark(1);
}

(int, int) ret_order_tuple(void) {
	defer ret_order_mark(20);
	return (ret_order_mark(10), 7);
}

int ret_order_tuple_ok(void) {
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
