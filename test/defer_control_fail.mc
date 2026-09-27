// fail: F:deferred statement cannot contain a control transfer or another defer

void bad() {
	defer {
		if (true) {
			return;
		}
	}
}
