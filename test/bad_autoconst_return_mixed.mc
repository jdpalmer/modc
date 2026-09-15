char* leak_mixed(int pick) {
	char buf[8] = { 0 };
	buf[0] = (char)'a';
	buf[1] = (char)0;
	if (pick) {
		return "literal";
	}
	return buf;
}

int main(void) {
	(void)leak_mixed(0);
	return 0;
}
