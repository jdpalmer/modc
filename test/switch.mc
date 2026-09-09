/* switch, goto, labels, and fallthrough; */

int classify(int x) {
	switch (x) {
		case 1: return 10;
		case 2: return 20;
		case 3: case 4: return 30;
		default: return 0;
	}
}

int case_fall(int x) {
	int s = {0};
	s = 0;
	switch (x) {
		case 1: s = s + 1;
		fallthrough;
		case 2: s = s + 2;
		break;
		case 3: s = 3;
		break;
	}
	return s;
}

int case_range(int x) {
	switch (x) {
		case 1 .. 3: return 100;
		case 10 .. 12: return 200;
		case 5: return 50;
		default: return 0;
	}
}

int withbreak(int x) {
	int i = {0};
	int s = {0};
	s = 0;
	for (i = 0; i < 10; i++) {
		switch (i) {
			case 3: break;
			default: s = s + i;
			continue;
		}
		break;
	}
	return s + x;
}

int skipto(int x) {
	if (x < 0) {
		goto neg;
	}
	if (x == 0) {
		goto zero;
	}
	return x + 1;
	zero: return 100;
	neg: return -1;
}

int loop_goto(int n) {
	int i = {0};
	int s = {0};
	s = 0;
	i = 0;
	again: if (i >= n) {
		goto done;
	}
	s = s + i;
	i++;
	goto again;
	done: return s;
}
