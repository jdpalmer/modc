char* leak_mixed(int pick) {
	char buf[8] = { 0 };
	buf[0] = 'a';
	buf[1] = 0;
	if (pick) {
		return "literal";
	}
	return buf;
}

int main() {
	(void)leak_mixed(0);
	return 0;
}
