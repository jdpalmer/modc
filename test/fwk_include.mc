#include <Dummy/Dummy.h>

int dummy_value(void) {
	return DUMMY_MAGIC;
}

int main(void) {
	return dummy_value() == 42 ? 0: 1;
}
