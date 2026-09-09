import "../pkg_log";

int main(void) {
	log_info("x");
	return log_code() == 42 ? 0: 1;
}
