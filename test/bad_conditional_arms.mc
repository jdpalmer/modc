struct Value {
	int x;
};

int bad_conditional_arms(int choose) {
	struct Value value = { 1 };
	int* pointer = { 0 };
	choose ? pointer : value;
	return 0;
}
