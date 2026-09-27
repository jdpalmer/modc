// fail: F:undeclared identifier Priv

import "pkg_priv";

int bad() {
	Priv p = { 0 };
	(void)p;
	return 0;
}
