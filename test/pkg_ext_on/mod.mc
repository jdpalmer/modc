import "pkg_ext_base";

int (Box* b).get() {
	return b.n;
}

void (Box* b).set(int v) {
	b.n = v;
}
