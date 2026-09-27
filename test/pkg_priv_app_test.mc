/* Portable test (was pkg_priv_app.mc + pkg_priv_app_main.c). */
#include <assert.h>

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

int main() {
	return pkg_priv_run();
}
