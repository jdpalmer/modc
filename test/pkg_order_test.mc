import "pkg_order";

int main() {
	Point pt = { 20, 22 };
	Outer o = { pt };
	if (pt.sum() != 42) {
		return 1;
	}
	return outer_sum(&o) == 42 ? 0: 2;
}
