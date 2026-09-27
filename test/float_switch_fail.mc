// fail: F:switch expression must be an integer

int bad_float_switch(double value) {
	switch (value) {
	case 1:
		return 1;
	default:
		return 0;
	}
}
