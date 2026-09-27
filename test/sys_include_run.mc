/* Platform header via auto system includes (not modc stubs). */

#include <modc_system_probe.h>

int sys_include_smoke() {
	return getpid() > 0 ? 0: 1;
}
