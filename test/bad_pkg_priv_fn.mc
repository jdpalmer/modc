import "pkg_priv";

int bad() {
	return priv_add(1, 2);
}
