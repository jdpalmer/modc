/* #embed → char[..] via ranged conversion. */
#include <assert.h>

int main() {
	const char[..] v =
#embed "embed_data/abc.bin"
	;
	assert(len(v) == 3);
	assert(v[0] == 'A' && v[2] == 'C');
	return 0;
}
