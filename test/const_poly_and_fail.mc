// fail: F:implicit conversion from

const? char[..] pick(const? char[..] a, const? char[..] b) {
	(void)b;
	return a;
}

int main() {
	/* Both const? args const => const result; cannot assign to mutable. */
	char buf[2] = { 0, 0 };
	char[..] bad = pick("aa", "bb");
	(void)bad;
	(void)buf;
	return 0;
}
