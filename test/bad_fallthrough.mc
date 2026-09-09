int bad(int x) {
	int s = {0};
	s = 0;
	switch (x) {
		case 1: s = 1;
		case 2: s = 2;
		break;
	}
	return s;
}
