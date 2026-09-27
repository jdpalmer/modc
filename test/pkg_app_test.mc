/* Portable test (was pkg_app.mc + pkg_app_main.c). */
#include <assert.h>

import "pkg_log";

int pkg_use_log() {
	log_info("hi");
	return log_code();
}

int main() {
	assert_eq(pkg_use_log(), 42);
	return 0;
}
