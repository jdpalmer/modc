#include "shim.h"

struct Window {
	static FakeWidget* widget;
	int id;
};

int encap_touch(Window* w) {
	w.widget = 0;
	return w.id;
}

int encap_clear_widget(Window* w) {
	w.widget = 0;
	return 0;
}
