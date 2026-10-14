/* Call sites must use the full mangled link name (Val.text used to truncate at 96). */
#include <assert.h>

typedef struct LongTypeNameForValTextTruncationABCDEFGHIJKLMNOP {
	int x;
} LongTypeNameForValTextTruncationABCDEFGHIJKLMNOP;

/* Inflate body past InlineNodeBudget so the call is not inlined. */
void (LongTypeNameForValTextTruncationABCDEFGHIJKLMNOP* r).long_method_name_past_ninety_six_chars_zzzz(int v) {
	r.x = v;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = r.x + 1;
	r.x = r.x - 1;
	r.x = v;
}

int main() {
	LongTypeNameForValTextTruncationABCDEFGHIJKLMNOP o = { 0 };

	(&o).long_method_name_past_ninety_six_chars_zzzz(42);
	assert_eq(o.x, 42);
	return 0;
}
