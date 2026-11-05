/* Live method receiver with runtime checks (default). */
#include <assert.h>

struct T {
	int x;
};

int (T* t).get() {
	return t.x;
}

int main() {
	T t = { 7 };
	assert_eq(t.get(), 7);
	return 0;
}
