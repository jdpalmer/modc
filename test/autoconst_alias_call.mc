/* Local alias of param passed to inferred READONLY callee. */
void read_only(char* p) {
	if (p) {
		(void)p[0];
	}
}

void via_alias(char* p) {
	char* q = { 0 };
	q = p;
	read_only(q);
}

int autoconst_alias_call_ok(void) {
	via_alias("alias");
	return 0;
}
