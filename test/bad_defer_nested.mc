void cleanup() {
}

void bad() {
	defer defer cleanup();
}
