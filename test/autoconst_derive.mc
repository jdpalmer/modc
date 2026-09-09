/* Derived pointer used only for reads — still infer READONLY. */
static void read_offset(char* p) {
	char* q = {0};
	char c = {0};
	q = p + 1;
	c = q[0];
	(void)c;
}

int autoconst_derive_ok(void) {
	read_offset("ab");
	return 0;
}
