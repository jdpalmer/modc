/* File-scope globals and string literals */

int g;
int gi = 7;
static int s;

int get_g(void) {
	return g;
}

void set_g(int v) {
	g = v;
}

int get_gi(void) {
	return gi;
}

int bump_s(void) {
	s = s + 1;
	return s;
}

int first_char(void) {
	char* p = { 0 };
	p = "hi";
	return p[0];
}

int str_len3(void) {
	char* p = { 0 };
	p = "abc";
	return p[0] + p[1] + p[2];
}
