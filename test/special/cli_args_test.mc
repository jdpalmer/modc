int main(int argc, char** argv) {
	if (argc != 3) {
		return 1;
	}
	if (argv[1][0] != 'a' || argv[1][1] != 0) {
		return 2;
	}
	if (argv[2][0] != 'b' || argv[2][1] != 0) {
		return 3;
	}
	return 0;
}
