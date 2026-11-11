// fail: F:'vsprintf' is deprecated

#include <stdio.h>
#include <stdarg.h>

void go(char* buf, const char* fmt, ...) {
	va_list ap = { 0 };
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
}

int main() {
	char buf[32] = { 0 };
	go(buf, "%d", 1);
	return 0;
}
