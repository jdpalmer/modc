int sum8(int a[8]) {
	return a[0];
}

int bad() {
	int b[5] = { 0 };
	b[0] = 1;
	b[1] = 2;
	b[2] = 3;
	b[3] = 4;
	b[4] = 5;
	return sum8(b);
}
