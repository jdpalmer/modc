static enum { PrivCap = 3 };

static struct Priv {
	int n;
};

static int priv_add(int a, int b) {
	return a + b + PrivCap;
}

int (Priv* p).value() {
	return p.n;
}

int pkg_priv_cross(int x) {
	Priv p = { 0 };
	p.n = priv_add(x, 1);
	return p.value();
}
