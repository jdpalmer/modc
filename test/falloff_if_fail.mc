// fail: F:control reaches end of non-void function

int bad(int c) {
	if (c) {
		return 1;
	}
}
