#include <stdint.h>

extern int narrow_ok(int64_t);
extern int widen_ok(void);
extern int char_lit_ok(void);
extern int int_lit_fit(void);

int
main(void)
{
	if(narrow_ok(7) != 7)
		return 1;
	if(widen_ok() != 3)
		return 2;
	if(char_lit_ok() != 'a')
		return 3;
	if(int_lit_fit() != 112)
		return 4;
	return 0;
}
