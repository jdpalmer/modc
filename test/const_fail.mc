// fail: F:implicit conversion from pointer to pointer requires a cast

int main() {
	const char *p = "hi";
	char *q = { 0 };
	q = p;
	return 0;
}
