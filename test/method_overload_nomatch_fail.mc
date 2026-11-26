// fail: F:no matching overload for method get

struct Box {
	int n;
};

int (Box* b).get(int x) {
	return b.n + x;
}

int main() {
	Box b = { 1 };
	return b.get();
}
