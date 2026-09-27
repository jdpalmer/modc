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
	JsonValue v = { 0 };
	JsonValue* p = { 0 };
	JsonValue* kids[1] = { 0 };

	v.kind = 1;
	v.child = 0;
	v.items = 0;
	v.n = 0;
	kids[0] = &v;
	v.items = kids;
	v.n = 1;
	p = v.items[0];
	if (p == 0 || p.kind != 1) {
		return 1;
	}
	if (v.n != 1) {
		return 2;
	}
	return 0;
}

int main() {
	return self_ref_ptr_run();
}
