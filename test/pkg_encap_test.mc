import "pkg_encap";

int main() {
	Window w = { 0 };
	w.id = 3;
	return encap_touch(&w) == 3 ? 0: 1;
}
