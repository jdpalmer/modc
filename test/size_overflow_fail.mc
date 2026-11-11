// fail: F:allocation size overflows

int main() {
	/* 2^62 + 1 ints → byte size wraps on 64-bit size_t */
	int[..] s = alloc(int, 4611686018427387905UL);

	(void)s;
	return 0;
}
