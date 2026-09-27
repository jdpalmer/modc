// fail: F:control reaches end of non-void function

int bad(int x) {
	switch (x) {
		case 1: return 1;
	}
}
