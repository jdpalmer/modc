static void sink(char* p) {
	if (p) {
		p[0] = 'x';
	}
}

int main(void) {
	sink("hello");
	return 0;
}
