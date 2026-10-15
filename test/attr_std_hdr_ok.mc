#include "attr_std_hdr.h"

int main() {
	int n = { 0 };
	n = attr_std_nodiscard();
	(void)attr_std_unsequenced(1);
	(void)attr_std_reproducible(2);
	(void)attr_std_gnu_unused();
	(void)attr_std_maybe;
	return n;
}
