#include "asm_label_hdr.h"

int asm_label_fn(int x) {
	return x + 1;
}

int asm_label_fn2() {
	return 2;
}

int use_asm_labels() {
	return asm_label_fn(10) + asm_label_fn2();
}
