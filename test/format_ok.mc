#include <stdio.h>

int format_ok() {
	char buf[8] = { 0 };
	char[..] v = { 0 };
	int n = 42;

	buf[0] = 'h';
	buf[1] = 'i';
	v = ranged(buf, 2);
	printf("hello\n");
	printf("%s", v);
	printf("%d %s\n", n, "ok");
	printf("%c", '!');
	return 0;
}
