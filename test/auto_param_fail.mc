// fail: F:auto is only for initialized locals (auto x = expr)

void bad(auto x) {
	(void)x;
}
