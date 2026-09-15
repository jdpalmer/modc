import "pkg_order";

int main(void) {
	Point pt = { 0 };
	Outer o = { 0 };
	pt.x = 20;
	pt.y = 22;
	o.p = pt;
	if (pt.sum() != 42) {
		return 1;
	}
	return outer_sum(&o) == 42 ? 0: 2;
}
