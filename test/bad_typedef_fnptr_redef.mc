/* Conflicting function-pointer typedefs must still be rejected. */
typedef int (*Handler)(int);
typedef char (*Handler)(int);
