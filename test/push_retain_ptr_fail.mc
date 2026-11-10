// fail: F:owned value 'p' never released or retained

typedef struct TagEntry {
	int x;
} TagEntry;

int main() {
	TagEntry *[..] items = { 0 };
	TagEntry* p = alloc(TagEntry);

	defer release(&items);
	/* Failure path keeps ownership of p and falls through — must free. */
	if (!push(&items, p)) {
		(void)p;
	}
	return 0;
}
