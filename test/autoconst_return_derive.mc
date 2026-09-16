char* echo(char* msg) {
	return msg;
}

int autoconst_return_derive_ok() {
	char* p = { 0 };
	p = echo("hi");
	return p[0] == (char)'h' ? 0: 1;
}
