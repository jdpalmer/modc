import "pkg_ext_base";
import "pkg_ext_on";

int main() {
	Box b = { 0 };

	b.set(9);
	return b.get() == 9 ? 0 : 1;
}
