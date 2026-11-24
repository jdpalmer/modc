// fail: F:empty (T[..])
int main() {
	int[..] s = (int[..]){};

	(void)s;
	return 0;
}
