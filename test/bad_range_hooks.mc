struct NoHooks {
	int x;
};

int bad(void) {
	NoHooks n = { 0 };
	int t = { 0 };
	n.x = 1;
	t = 0;
	for (auto x: n)t = t + x;
	return t;
}
