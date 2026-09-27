// fail: F:method name show conflicts with field on Box

struct Box {
	int show;
};

void (Box* b).show() {
}

int main() {
	return 0;
}
