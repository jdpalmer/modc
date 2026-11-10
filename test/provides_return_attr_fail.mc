// fail: F:returning owned value requires [[provides]]

int* make() {
	int* z = alloc(int);

	return z;
}

int main() {
	return 0;
}
