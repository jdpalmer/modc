import "pkg_priv";

int bad(void) {
	Priv p = {
		0 };
	(void)p;
	return 0;
}
