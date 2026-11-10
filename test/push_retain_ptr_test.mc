/* Runtime: push retains owned pointer elems into T*[..]. */

typedef struct TagEntry {
	int x;
} TagEntry;

int main() {
	TagEntry *[..] items = { 0 };
	TagEntry* p = alloc(TagEntry);

	defer release(&items);
	*p = (TagEntry){ .x = 7 };
	if (!push(&items, p)) {
		return 1;
	}
	if (len(items) != 1) {
		return 2;
	}
	if (items[0].x != 7) {
		return 3;
	}
	if (!push(&items, alloc(TagEntry))) {
		return 4;
	}
	if (len(items) != 2) {
		return 5;
	}
	items[1].x = 9;
	if (items[1].x != 9) {
		return 6;
	}
	return 0;
}
