// fail: F:redefinition of method get

struct Box {
	int n;
};

int (Box* b).get() {
	return b.n;
}

int (Box* b).get() {
	return b.n + 1;
}

int main() {
	return 0;
}
