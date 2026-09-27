// fail: F:unsequenced modification of 'i'

int bad() {
	int i = { 0 };
	i = 0;
	i = i++;
	return i;
}
