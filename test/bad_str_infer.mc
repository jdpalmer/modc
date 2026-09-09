void writes_msg(char* msg) {
	if (msg) {
		msg[0] = (char)'x';
	}
}

int main(void) {
	writes_msg("nope");
	return 0;
}
