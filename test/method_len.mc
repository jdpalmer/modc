/* User method/func named len must not be hijacked by builtin len(). */
typedef struct S {
	int n;
}
S;

int (S* s).len(void) {
	return s.n;
}

int len(int n) {
	return n + 1;
}

int method_len_run(void) {
	S s = { 0 };
	int a[3] = { 0 };
	s.n = 4;
	if (s.len() != 4) {
		return 1;
	}
	if (len(2) != 3) {
		return 2;
	}
	/* Builtin still works when there is no user len in scope — see ranged.mc.
	 * Here user len shadows the builtin for bare len(...). */
	(void)a;
	return 0;
}
