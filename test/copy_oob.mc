/* len(src) > len(dst); must abort with bounds under runtime checks. */

int main() {
	int dst[2] = {0, 0};
	int src[3] = {1, 2, 3};

	(void)copy(dst, src);
	return 0;
}
