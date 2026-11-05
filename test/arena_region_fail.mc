// fail: F:cannot escape

import "arena";

char[..] bad_arena_view() {
	Arena a = { 0 };
	char[..] s = { 0 };
	a.init();
	defer a.free();
	auto (ok, v) = a.copy("hi");
	(void)ok;
	s = v;
	return s;
}

int main() {
	return 0;
}
