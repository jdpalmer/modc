// fail: F:goto jumps over a defer

void cleanup() {
}

int bad() {
	goto target;
	defer cleanup();
	target: return 0;
}
