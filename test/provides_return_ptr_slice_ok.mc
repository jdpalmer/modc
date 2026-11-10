// ok: return ptr(owned T[..]) hands off the header obligation
#include <stddef.h>
#include <stdlib.h>

[[provides]] char* from_alloc(int n) {
	char[..] buf = alloc(char, n);

	return ptr(buf);
}

[[provides]] char* from_alloc_via_local(int n) {
	char[..] buf = alloc(char, n);
	char* z = ptr(buf);

	return z;
}

[[provides]] char* direct_temp(int n) {
	return ptr(alloc(char, n));
}

int main() {
	char* a = from_alloc(4);
	char* b = from_alloc_via_local(4);
	char* c = direct_temp(4);

	free(a);
	free(b);
	free(c);
	return 0;
}
