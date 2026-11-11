/* Runtime smoke: retains-in-if + push-retain still compose. */

typedef struct Box {
	int x;
} Box;

bool take_box([[retains]] Box* p) {
	(void)p;
	return true;
}

int main() {
	Box *[..] items = { 0 };
	Box* p = alloc(Box);
	Box* q = alloc(Box);

	defer release(&items);
	*p = (Box){ .x = 1 };
	*q = (Box){ .x = 2 };
	if (!take_box(p)) {
		return 1;
	}
	if (!push(&items, q)) {
		return 2;
	}
	if (len(items) != 1 || items[0].x != 2) {
		return 3;
	}
	return 0;
}
