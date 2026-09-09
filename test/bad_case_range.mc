int bad(int x) {
	switch (x) {
		case 5 .. 1: return 1;
		default: return 0;
	}
}
