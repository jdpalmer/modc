int bad(int* p) {
	int t = { 0 };
	t = 0;
	for (auto x: p)t = t + x;
	return t;
}
