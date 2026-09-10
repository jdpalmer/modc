/* POSIX-style: tag and function share a spelling (separate C namespaces). */
#pragma once

struct if_nameindex_stub {
	unsigned int if_index;
	char *if_name;
};

struct if_nameindex_stub *if_nameindex_stub(void);
