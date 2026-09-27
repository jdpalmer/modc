/* #embed: brace and brace-less array init; limit(); binary bytes. */
#include <assert.h>

static char brace[] = {
#embed "embed_data/abc.bin"
};

static char bare[] =
#embed "embed_data/abc.bin"
;

static char clipped[2] = {
#embed "embed_data/abc.bin" limit(2)
};

static char raw[] = {
#embed "embed_data/bin.bin"
};

int main() {
	assert(sizeof(brace) == 3);
	assert(brace[0] == 'A' && brace[1] == 'B' && brace[2] == 'C');
	assert(sizeof(bare) == 3);
	assert(bare[0] == 'A' && bare[2] == 'C');
	assert(sizeof(clipped) == 2);
	assert(clipped[0] == 'A' && clipped[1] == 'B');
	assert(sizeof(raw) == 3);
	assert(raw[0] == 0 && raw[1] == 1 && raw[2] == 255);
	return 0;
}
