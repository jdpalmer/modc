void log_info(char* msg) {
	(void)msg;
}

static int log_hidden() {
	return 42;
}

int log_code() {
	return log_hidden();
}
