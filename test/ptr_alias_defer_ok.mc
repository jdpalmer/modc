// ok: named ptr(slice) aliases stay usable under defer / pool until release
#include <stddef.h>
#include <stdio.h>

void use(char* p) {
	(void)p;
}

int heap_path() {
	char[..] s = alloc(char, 8);

	defer release(&s);
	s[0] = 'a';
	s[1] = 0;
	char* z = ptr(s);
	use(z);
	use(z);
	return 0;
}

int pool_path() {
	pool p {
		char[..] s = { 0 };

		p.copy(&s, "hello");
		char* z = ptr(s);
		use(z);
		use(z);
	}
	return 1;
}

int main() {
	return heap_path() + pool_path();
}
