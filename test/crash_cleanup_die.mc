/* Cleanup runs before abort on panic. */
import "crash";
#include <unistd.h>

static int cleaned = 0;

static void on_fatal() {
	cleaned = 1;
	(void)write(2, "cleaned\n", 8);
}

int main() {
	crash_init(on_fatal);
	panic("after-cleanup");
	return cleaned;
}
