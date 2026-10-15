/* Headers may carry C23 [[...]] attributes; unknown / opt attrs are ignored. */
#pragma once

[[nodiscard]] int attr_std_nodiscard(void);
[[maybe_unused]] int attr_std_maybe;
[[unsequenced]] int attr_std_unsequenced(int x);
[[reproducible]] int attr_std_reproducible(int x);
[[gnu::unused]] int attr_std_gnu_unused(void);
