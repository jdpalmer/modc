// fail: F:auto is only for initialized locals (auto x = expr)

auto x = 1;

int bad() {
	return x;
}
