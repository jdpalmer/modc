// fail: F:type size of

struct S {
	char a[1000000000];
	char b[1000000000];
	char c[1000000000];
};

int main() {
	struct S s;
	(void)s;
	return 0;
}
