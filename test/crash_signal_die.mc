/* Fault signal: cleanup then a short stderr line, then re-raise. */
import "crash";
#include <signal.h>
#include <unistd.h>

static void on_fatal() {
	(void)write(2, "cleaned\n", 8);
}

int main() {
	crash_init(on_fatal);
	raise(SIGSEGV);
	return 0;
}
