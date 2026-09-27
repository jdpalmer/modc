// fail: F:undeclared identifier PrivCap

import "pkg_priv";

int bad() {
	return PrivCap;
}
