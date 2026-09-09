void log_info(char* msg) {
	(void)msg;
}

static int log_hidden(void) {
	return 42;
}

int log_code(void) {
	return log_hidden();
}
