struct Box {
	int n;
};

int (Box* b).get() {
	return b.n;
}

int (Box* b).get(int scale) {
	return b.n * scale;
}

void (Box* b).set(int v) {
	b.n = v;
}

void (Box* b).set(int v, int add) {
	b.n = v + add;
}

int main() {
	Box b = { 0 };
	b.set(3);
	if (b.get() != 3) {
		return 1;
	}
	b.set(2, 5);
	if (b.get() != 7) {
		return 2;
	}
	if (b.get(4) != 28) {
		return 3;
	}
	return 0;
}
