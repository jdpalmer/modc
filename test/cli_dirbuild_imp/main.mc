import "../pkg_log";

int main() {
	log_info("x");
	return log_code() == 42 ? 0: 1;
}
