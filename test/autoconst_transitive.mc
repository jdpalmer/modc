/* v2 fixpoint: a -> b -> c, all read-only; literal allowed at top. */
void leaf_read(char* msg) {
	(void)msg;
}

void mid_read(char* msg) {
	leaf_read(msg);
}

void top_read(char* msg) {
	mid_read(msg);
}

int autoconst_transitive_ok() {
	top_read("chain");
	return 0;
}
