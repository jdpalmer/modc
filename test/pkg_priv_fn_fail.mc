// fail: F:undeclared identifier priv_add

import "pkg_priv";

int bad() {
	return priv_add(1, 2);
}
