/* [[nodiscard]] is silenced by assign or (void); unmarked returns may be dropped. */

[[nodiscard]] int must_use() {
	return 7;
}

int plain() {
	return 3;
}

int ok() {
	int x = { 0 };
	x = must_use();
	(void)must_use();
	plain();
	return x;
}
