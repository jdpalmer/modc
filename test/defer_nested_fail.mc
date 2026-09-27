// fail: F:deferred statement cannot contain a control transfer or another defer

void cleanup() {
}

void bad() {
	defer defer cleanup();
}
