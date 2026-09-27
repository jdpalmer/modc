// fail: F:left operand of assignment is not a modifiable lvalue

int main() {
	const char[..] name = "James";
	name[0] = 'x';
	return 0;
}
