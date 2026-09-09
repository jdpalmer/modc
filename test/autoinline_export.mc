/* Auto-inline: non-static small helper inlined at calls; symbol still emitted. */
int add(int a, int b) {
	return a + b;
}

int autoinline_export_run(void) {
	if (add(4, 6) != 10) {
		return 1;
	}
	return 0;
}
