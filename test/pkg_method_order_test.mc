import "pkg_method_order";

int main() {
	Wid w = { 0 };
	w.n = 7;
	return w.get() == 7 ? 0: 1;
}
