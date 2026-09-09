/* struct __declspec(...) Tag { } after a forward decl must not redefinition-error. */
struct _DepTag;
typedef struct _DepTag DepTag;

struct __declspec(deprecated("x")) _DepTag {
	int x;
};
