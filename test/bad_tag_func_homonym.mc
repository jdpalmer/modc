/* User .mc keeps a unified tag/ordinary namespace — no C-style homonyms. */
struct clash {
	int x;
};

int clash() {
	return 0;
}
