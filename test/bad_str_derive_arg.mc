static void write_byte(char* p) {
	if (p) {
		p[0] = (char)'y';
	}
}

static void pass_offset(char* p) {
	write_byte(p + 1);
}

int main(void) {
	pass_offset("ab");
	return 0;
}
