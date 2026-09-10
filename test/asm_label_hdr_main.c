/* Host cc harness for asm_label_hdr.mc */
int use_asm_labels(void);

int main(void) {
	return use_asm_labels() == 13 ? 0 : 1;
}
