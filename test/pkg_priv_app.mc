import "pkg_priv";

int pkg_priv_run() {
	if (pkg_priv_cross(1) != 5) {
		return 1;
	}
	if (pkg_priv_from_sib() != 33) {
		return 2;
	}
	return 0;
}
