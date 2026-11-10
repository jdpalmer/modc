// ok:
/* Scalar and pointer-only unions remain legal; aggregate overlays do not. */

union Scalars {
	int i;
	float f;
};

union Ptrs {
	void* any;
	char* s;
};

int main() {
	union Scalars a = { 1 };
	union Ptrs b = { 0 };
	(void)a.i;
	(void)b.any;
	return 0;
}
