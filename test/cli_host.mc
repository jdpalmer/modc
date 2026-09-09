/* Host target macros injected by the driver (no -D needed). */

#if defined( __APPLE__) || defined( __linux__) || defined( _WIN32)
#if defined( __x86_64__) || defined( __amd64__) || defined( __i386__) || defined( __aarch64__) || defined( __arm64__) || defined( __arm__) || defined( __riscv)
int host_ok(void) {
	return 1;
}
#else
int host_ok(void) {
	return 0;
}
#endif
#else
int host_ok(void) {
	return 0;
}
#endif
