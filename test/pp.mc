/* Plan 9–style preprocessor: define, include, ifdef */

#include "pp_inc.h"

#define LOCAL_SCALE 2

#ifdef PP_ANSWER
int from_include() {
	return PP_ANSWER;
}
#else
int from_include() {
	return 0;
}
#endif

#ifndef NO_SUCH_MACRO
int ifndef_ok() {
	return 1;
}
#else
int ifndef_ok() {
	return 0;
}
#endif

int macro_add(int a, int b) {
	return PP_ADD(a, b);
}

int local_def() {
	return LOCAL_SCALE * 21;
}

#ifdef LOCAL_SCALE
#undef LOCAL_SCALE
#endif

#ifndef LOCAL_SCALE
int after_undef() {
	return 7;
}
#else
int after_undef() {
	return 0;
}
#endif
