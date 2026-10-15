// fail: F:discarded [[nodiscard]] return value; assign it or cast to void

#include "attr_std_hdr.h"

int bad() {
	attr_std_nodiscard();
	return 0;
}
