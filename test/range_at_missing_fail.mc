// fail: F:range-for hooks require both range_count and range_at

#include <stddef.h>

struct OnlyCount {
	int n;
};

overload size_t range_count(OnlyCount b, int** out_ptr) {
	*out_ptr = 0;
	(void)b;
	return 0;
}

int main() {
	OnlyCount b = { 3 };
	int s = 0;
	for (auto x : b) {
		s = s + x;
	}
	return s;
}
