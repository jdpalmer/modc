int bad(int x) {
	switch (x) {
		case 1 ... 3: return 1;
		default: return 0;
	}
}
