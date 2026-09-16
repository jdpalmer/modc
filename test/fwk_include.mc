#include <Dummy/Dummy.h>

int dummy_value() {
	return DUMMY_MAGIC;
}

int main() {
	return dummy_value() == 42 ? 0: 1;
}
