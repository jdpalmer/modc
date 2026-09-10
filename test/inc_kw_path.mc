/* Angled include path must keep keyword segments (e.g. net/if.h). */

#include <net/if.h>

int inc_kw_path_run(void) {
	return INC_KW_OK == 7 ? 0 : 1;
}
