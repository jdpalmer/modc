#include <stdio.h>
#include <string.h>

int main() {
	char dst[16] = { 0 };
	char src[4] = { 'h', 'i', 0 };

	strncpy(dst, src, sizeof(dst));
	dst[sizeof(dst) - 1] = 0;
	snprintf(dst, sizeof(dst), "%s", src);
	return 0;
}
