/* Headers may declare a tag and a function with the same name. */

#include "hdr_tag_homonym.h"

int hdr_tag_homonym_run(void) {
	struct if_nameindex_stub *p = if_nameindex_stub();

	(void)p;
	return 0;
}
