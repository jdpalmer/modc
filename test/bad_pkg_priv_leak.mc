static struct Hidden {
	int x;
};

Hidden leak_hidden(void) {
	Hidden h = { 0 };
	return h;
}
