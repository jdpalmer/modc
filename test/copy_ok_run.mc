/* run: 0 */

int main() {
	int dst[4] = {0, 0, 0, 0};
	int src[3] = {1, 2, 3};
	int[..] d = dst;
	int[..] s = src;

	if (!copy(d, s)) {
		return 1;
	}
	if (dst[0] != 1 || dst[1] != 2 || dst[2] != 3 || dst[3] != 0) {
		return 2;
	}
	/* Fixed array destination */
	if (!copy(dst, (int[2]){9, 8})) {
		return 3;
	}
	if (dst[0] != 9 || dst[1] != 8 || dst[2] != 3) {
		return 4;
	}
	return 0;
}
