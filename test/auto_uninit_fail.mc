// fail: F:auto is only for initialized locals (auto x = expr)

int bad() {
	auto x;
	return 0;
}
