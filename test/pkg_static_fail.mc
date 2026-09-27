// fail: F:undeclared identifier log_hidden

import "pkg_log";

int bad() {
	return log_hidden();
}
