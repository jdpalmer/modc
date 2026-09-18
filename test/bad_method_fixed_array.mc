struct Sink {
	int n;
};

int (Sink* s).take(int a[3]) {
	return s.n + a[0];
}

int bad() {
	Sink s = { 0 };
	int a[2] = { 0 };
	return s.take(a);
}
