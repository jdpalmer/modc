// fail: F:case ranges use '..' ('...' is only for varargs)

int bad(int x) {
	switch (x) {
		case 1 ... 3: return 1;
		default: return 0;
	}
}
