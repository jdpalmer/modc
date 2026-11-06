#include <stdio.h>

int main() {
	float v[3] = { 0 };

	v[0] = 1;
	float[..] s = ranged(v, 3);
	s[1] = 2;
	s[2] = 3;
	if (v[0] != 1 || s[1] != 2 || v[2] != 3) {
		return 1;
	}
	printf("ok\n");
	return 0;
}
