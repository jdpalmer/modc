static void sink(char* p) {
	if (p) {
		p[0] = (char)'x';
	}
}

int main(void) {
	sink("hello");
	return 0;
}
