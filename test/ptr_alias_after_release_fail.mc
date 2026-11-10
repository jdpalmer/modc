// fail: F:use of released value
#include <stddef.h>

void use(char* p) {
	(void)p;
}

int main() {
	char[..] s = alloc(char, 8);

	s[0] = 'a';
	s[1] = 0;
	char* z = ptr(s);
	release(&s);
	use(z);
	return 0;
}
