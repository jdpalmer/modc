import "pkg_priv";

int bad(void) {
	return priv_add(1, 2);
}
