static struct Hidden {
	int x;
};

Hidden leak_hidden() {
	Hidden h = { 0 };
	return h;
}
