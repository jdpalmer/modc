// fail: F:passing stack address to [[retained]] parameter 'p' of 'pkg_retain_keep'

import "pkg_retain";

int bad() {
	int x = 0;
	pkg_retain_keep(&x);
	return 0;
}
