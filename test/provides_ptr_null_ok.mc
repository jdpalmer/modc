// ok: ptr(s) == NULL clears owned obligation on the failure path
#include <stddef.h>

int main() {
	int[..] data = alloc(int, 4);

	if (ptr(data) == NULL) {
		return 0;
	}
	if (!ptr(data)) {
		return 0;
	}
	release(&data);
	return 1;
}
