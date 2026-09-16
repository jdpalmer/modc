#include <string.h>

void take_ranged(char[..] x) {
	(void)x;
}

int strlen_local() {
	char[..] name = { 0 };
	int n = { 0 };
	name = "James";
	n = (int)strlen(name);
	if (n != 5) {
		return 0;
	}
	return 1;
}

int strlen_temp() {
	return (int)strlen("Isaac");
}

int pass_ranged() {
	char[..] name = { 0 };
	name = "James";
	take_ranged(name);
	take_ranged("Isaac");
	return 1;
}

int mutable_ok() {
	char buf[8] = { 0 };
	char[..] view = { 0 };
	buf[0] = 'a';
	buf[1] = 'b';
	buf[2] = 'c';
	buf[3] = 0;
	view = buf;
	view[0] = 'x';
	if (buf[0] != 'x') {
		return 0;
	}
	return 1;
}
