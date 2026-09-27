// fail: F:public function 'leak_hidden' must not use a static (package-private) type

static struct Hidden {
	int x;
};

Hidden leak_hidden() {
	Hidden h = { 0 };
	return h;
}
