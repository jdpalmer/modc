// fail: F:cannot pass ranged array to pointer; use ptr()

void take(void *p) {
	(void)p;
}

int main() {
	int buf[4] = { 0 };
	int[..] s = ranged(buf, 4);

	take(s);
	return 0;
}
