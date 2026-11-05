// fail: F:initializer of 'x' is never used

int both_assign(int c) {
	int x = 0;
	if (c) {
		x = 1;
	} else {
		x = 2;
	}
	return x;
}

int straight() {
	int y = 0;
	y = 1;
	return y;
}

int main() {
	return 0;
}
