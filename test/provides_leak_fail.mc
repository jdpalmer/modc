// fail: F:owned value 'z' never released or retained

int main() {
	int* z = alloc(int);

	(void)z;
	return 0;
}
