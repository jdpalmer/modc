#include "autoconst_hdr.h"

int main(void) {
	char* p = {0};
	p = hdr_msg();
	p[0] = (char)'X';
	return 0;
}
