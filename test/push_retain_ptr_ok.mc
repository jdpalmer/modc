/* ok: push of owned T* into T*[..] retains on success */

typedef struct TagEntry {
	int x;
} TagEntry;

int named_checked() {
	TagEntry *[..] items = { 0 };
	TagEntry* p = alloc(TagEntry);

	defer release(&items);
	*p = (TagEntry){ 0 };
	if (!push(&items, p)) {
		return 1;
	}
	return 0;
}

int expr_checked() {
	TagEntry *[..] items = { 0 };

	defer release(&items);
	if (!push(&items, alloc(TagEntry))) {
		return 1;
	}
	return 0;
}

int unchecked_stmt() {
	TagEntry *[..] items = { 0 };
	TagEntry* p = alloc(TagEntry);

	defer release(&items);
	*p = (TagEntry){ .x = 1 };
	push(&items, p);
	return 0;
}

int scalar_still_ok() {
	int[..] nums = { 0 };

	defer release(&nums);
	if (!push(&nums, 42)) {
		return 1;
	}
	return 0;
}

int main() {
	return 0;
}
