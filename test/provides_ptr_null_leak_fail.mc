// fail: F:owned value 'data' never released or retained

int main() {
	int[..] data = alloc(int, 4);

	/* No null check and no release — still a leak even if ptr may be null. */
	(void)ptr(data);
	return 0;
}
