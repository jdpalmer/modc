// fail: F:left operand of assignment is not a modifiable lvalue

int main() {
	const char *s = "hello";
	s[0] = 72;
	return 0;
}
