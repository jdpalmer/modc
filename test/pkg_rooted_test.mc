/* [package] root= remaps identity dir → src/ for import and method mangling. */
#include <assert.h>

import "pkg_rooted";

int
main()
{
	Box b = { 3 };

	assert_eq(pkg_rooted_val(), 11);
	assert_eq(b.scaled(4), 12);
	return 0;
}
