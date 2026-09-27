/* Portable test (was cli_host.mc + cli_host_main.c). */
#include <assert.h>

/* Host target macros injected by the driver (no -D needed). */

#if defined( __APPLE__) || defined( __linux__) || defined( _WIN32)
#if defined( __x86_64__) || defined( __amd64__) || defined( __i386__) || defined( __aarch64__) || defined( __arm64__) || defined( __arm__) || defined( __riscv)
int host_ok() {
	return 1;
}
#else
int host_ok() {
	return 0;
}
#endif
#else
int host_ok() {
	return 0;
}
#endif

int main() {
	assert_eq(host_ok(), 1);
	return 0;
}
