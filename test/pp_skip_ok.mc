/* Dead #if branches must not expand or emit (directive fast-forward). */

#if 0
/* Would fail typecheck / undeclared if this branch were taken. */
this_is_not_a_declaration
#define SHOULD_NOT_EXIST 1
#include "no_such_header_pp_skip.h"
#if 1
still_dead
#endif
#error "dead branch must not run"
#endif

#if 1
#define SKIP_LIVE 1
#else
dead_else_arm
#define SKIP_LIVE 0
#endif

#if 0
#elif 0
also_dead
#elif 1
#define SKIP_ELIF 1
#else
nope
#endif

#ifdef SHOULD_NOT_EXIST
#define SKIP_BAD 1
#else
#define SKIP_BAD 0
#endif

int pp_skip_result() {
	return SKIP_LIVE + SKIP_ELIF + SKIP_BAD;
}
