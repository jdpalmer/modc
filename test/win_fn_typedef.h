/* Regression: function definition (no trailing ';') must not steal the next typedef in prescan. */
typedef unsigned int DWORD;
typedef void VOID;

VOID
AfterFnHelper(DWORD x)
{
	(void)x;
	return;
}

typedef struct _AfterFnStruct {
	DWORD Field;
} AfterFnStruct;

typedef AfterFnStruct *PAfterFnStruct;
