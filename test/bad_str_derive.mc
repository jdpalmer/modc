/* Store through p+1 must keep param mutable (richer derivation). */
static void write_offset(char* p) {
	char* q = { 0 };
	q = p + 1;
	q[0] = 'x';
}

int main(void) {
	write_offset("ab");
	return 0;
}
