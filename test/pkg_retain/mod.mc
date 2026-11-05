/* Cross-package [[retained]] / return-flow (also exercised via cache export). */

int* pkg_retain_slot;

void pkg_retain_keep([[retained]] int* p) {
	pkg_retain_slot = p;
}

int* pkg_retain_id(int* p) {
	return p;
}
