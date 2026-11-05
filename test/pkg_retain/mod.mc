/* Cross-package [[retains]] / return-flow (also exercised via cache export). */

int* pkg_retain_slot;

void pkg_retain_keep([[retains]] int* p) {
	pkg_retain_slot = p;
}

int* pkg_retain_id(int* p) {
	return p;
}
