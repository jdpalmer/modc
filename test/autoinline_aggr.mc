/* Auto-inline: small aggregate returns (tuple / char[..]) expand at call sites. */
static(int, int) pair(int a, int b) {
	return (a, b);
}

static(int, bool) maybe(int x) {
	if (x < 0) {
		return (0, false);
	}
	return (x, true);
}

static char[..] take_view(char[..] s) {
	return s;
}

int autoinline_aggr_run() {
	auto (a, b) = pair(2, 3);
	char buf[4] = { 0 };
	char[..] v = { 0 };
	if (a + b != 5) {
		return 1;
	}
	{
		auto (n, ok) = maybe(7);
		if (!ok || n != 7) {
			return 2;
		}
	}
	{
		auto (n, ok) = maybe(-1);
		if (ok || n != 0) {
			return 3;
		}
	}
	buf[0] = 'x';
	buf[1] = 'y';
	v = take_view(ranged(buf, 2));
	if (len(v) != 2 || v[0] != 'x') {
		return 4;
	}
	return 0;
}
