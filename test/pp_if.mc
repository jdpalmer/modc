/* #if / #elif / defined() smoke test */

#if 1
#define IF1 1
#else
#define IF1 0
#endif

#if 0
#define IF0 1
#else
#define IF0 2
#endif

#if defined( PP_IF_EXTRA) || ! defined( NO_SUCH_MACRO)
#define IFDEF_OK 1
#else
#define IFDEF_OK 0
#endif

#if 0
#elif 1
#define ELIF_OK 1
#else
#define ELIF_OK 0
#endif

#if ! defined( NO_SUCH_MACRO) && ( 1 + 2 == 3)
#define EXPR_OK 1
#else
#define EXPR_OK 0
#endif

#if 1
#if 0
#define NEST 0
#else
#define NEST 1
#endif
#else
#define NEST 2
#endif

/* C &&/|| must parse both sides even when the value is already known */
#if 1 || 0
#define OR_OK 1
#else
#define OR_OK 0
#endif

#define PP_IF_X 1
#if ! defined( PP_IF_X) && 1
#define AND_SKIP 0
#else
#define AND_SKIP 1
#endif

#if 1 /* trailing comment */
#define CMT_OK 1
#else
#define CMT_OK 0
#endif

int pp_if_result() {
	return IF1 + IF0 + IFDEF_OK + ELIF_OK + EXPR_OK + NEST + OR_OK + AND_SKIP + CMT_OK;
}
