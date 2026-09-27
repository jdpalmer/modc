// fail: F:raw pointer is not iterable in range-for; use ranged(ptr, len) or a fixed array

int bad(int* p) {
	int t = { 0 };
	t = 0;
	for (auto x: p)t = t + x;
	return t;
}
