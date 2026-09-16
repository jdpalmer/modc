void writes_msg(char* msg) {
	if (msg) {
		msg[0] = 'x';
	}
}

int main() {
	writes_msg("nope");
	return 0;
}
