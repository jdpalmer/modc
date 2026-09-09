import "pkg_log";

int pkg_use_log(void) {
	log_info("hi");
	return log_code();
}
