// fail: F:cannot pass char[..] to

#include <stdio.h>

FILE* open_slice(const char[..] path) {
	return fopen(path, "r");
}

int main() {
	return 0;
}
