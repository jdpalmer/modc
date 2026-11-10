/* Portable test (was self_ref_ptr.mc + self_ref_ptr_main.c). */
#include <assert.h>

/* Self-referential pointer fields (T* / T**) must not crash the compiler. */
#include <stddef.h>

typedef struct JsonValue {
	int kind;
	struct JsonValue* child;
	struct JsonValue** items;
	size_t n;
}
JsonValue;

int self_ref_ptr_run() {
	JsonValue v = { 1, 0, 0, 0 };
	JsonValue* kids[1] = { 0 };

	kids[0] = &v;
	v.items = kids;
	v.n = 1;
	{
		JsonValue*[..] items = ranged(v.items, (int)v.n);
		JsonValue* p = items[0];

		if (p == 0 || p.kind != 1) {
			return 1;
		}
	}
	if (v.n != 1) {
		return 2;
	}
	return 0;
}

int main() {
	return self_ref_ptr_run();
}
