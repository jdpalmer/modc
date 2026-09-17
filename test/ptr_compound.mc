struct Pair {
	int x;
	int y;
};

int pointer_compound_ok() {
	int values[5] = {
		10, 20, 30, 40, 50 };
	struct Pair pairs[3] = { 0 };
	int* p = values;
	struct Pair* q = pairs;

	p += 2;
	if (*p != 30) {
		return 0;
	}
	p -= 1;
	q += 2;
	return *p == 20 && q == &pairs[2];
}
