/* File-scope globals and string literals */

int g;
int gi = 7;
static int s;
int ga[3] = { 1, 2, 3 };
int* gp = &g;
int* gap = ga + 1;
int* gai = &ga[2];

int twice(int x) {
	return x * 2;
}

int(*gfp)(int) = twice;

int get_g() {
	return g;
}

void set_g(int v) {
	g = v;
}

int get_gi() {
	return gi;
}

int bump_s() {
	s = s + 1;
	return s;
}

int global_ptrs() {
	return gp == &g && gap == &ga[1] && *gap == 2 && gai == &ga[2] &&
	       *gai == 3 && gfp(21) == 42;
}

int first_char() {
	char* p = { 0 };
	p = "hi";
	return p[0];
}

int str_len3() {
	char* p = { 0 };
	p = "abc";
	return p[0] + p[1] + p[2];
}
