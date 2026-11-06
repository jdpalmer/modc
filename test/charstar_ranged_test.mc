/* Multi-package: import pulls a package that indexes char*[..] into char*. */
import "charstar_env";

#include <assert.h>

int main() {
	assert(charstar_env_scan() >= 0);
	return 0;
}
