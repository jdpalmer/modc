void bad() {
	defer {
		if (true) {
			return;
		}
	}
}
