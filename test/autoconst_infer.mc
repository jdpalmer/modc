/* Callee does not store through msg — v2 should infer READONLY. */
void reads_msg(char* msg) {
	(void)msg;
}

/* Callee stores — must stay mutable; caller cannot pass a literal. */
void writes_msg(char* msg) {
	if (msg) {
		msg[0] = (char)'x';
	}
}

int infer_ok(void) {
	reads_msg("hi");
	writes_msg((char*)"z");
	return 0;
}
