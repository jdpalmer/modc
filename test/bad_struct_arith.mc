struct Value {
	int x;
};

int bad_struct_add() {
	struct Value a = { 1 };
	struct Value b = { 2 };
	a + b;
	return 0;
}
