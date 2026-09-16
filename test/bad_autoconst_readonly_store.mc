#include "autoconst_hdr.h"

int main() {
	char* p = { 0 };
	p = hdr_msg();
	p[0] = 'X';
	return 0;
}
