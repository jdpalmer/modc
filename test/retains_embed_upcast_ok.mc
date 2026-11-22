// ok:
/* [[retains]] consumes owned local through embed upcast (non-zero offset). */
#include <stdlib.h>

struct Widget {
	int x;
};

struct Label {
	int tag;
	Widget;
};

void group_add([[retains]] Widget* child) {
	(void)child;
}

/* Implicit Label* → Widget* inserts (Widget*)((char*)l + off). */
Label* make_parented() {
	Label* l = alloc(Label);

	group_add(l);
	return l;
}

/* Explicit cast still clears ownership. */
Label* make_cast() {
	Label* l = alloc(Label);

	group_add((Widget*)l);
	return l;
}

int main() {
	Label* a = make_parented();
	Label* b = make_cast();

	(void)a;
	(void)b;
	return 0;
}
