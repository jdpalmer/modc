#pragma modc c_sources( shim/add_one.c)
#include "shim/add_one.h"

int pkg_add_one(int x) {
	return c_add_one(x);
}
