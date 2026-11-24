// ok:
#include <stdint.h>
#include <stddef.h>

/* Same-width sign flips need casts; literals and widenings stay implicit. */

int main() {
	int32_t i = 1;
	uint32_t u = (uint32_t)i;
	int32_t j = (int32_t)u;
	uint32_t from_lit = 42;
	int64_t wide = i; /* signed widen */
	int64_t x = 7;
	size_t n = (size_t)x; /* same-width signed↔unsigned on LP64 */
	bool b = 1;
	char c = b; /* bool ↔ char stays implicit */

	(void)j;
	(void)from_lit;
	(void)wide;
	(void)n;
	(void)c;
	return u == 1u ? 0 : 1;
}
