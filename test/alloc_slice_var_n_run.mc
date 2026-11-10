// ok: alloc(T, n) / p.alloc(T, n) with runtime n
#include <assert.h>
#include <stddef.h>

int main() {
	int n = 5;
	int[..] heap = alloc(int, n);

	assert(ptr(heap) != NULL);
	assert(len(heap) == 5);
	assert(cap(heap) == 5);
	heap[4] = 40;
	assert(heap[4] == 40);
	release(&heap);

	pool p {
		int[..] s = p.alloc(int, n);

		assert(ptr(s) != NULL);
		assert(len(s) == 5);
		assert(cap(s) == 5);
		s[4] = 41;
		assert(s[4] == 41);
	}
	return 0;
}
