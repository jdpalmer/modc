#include <stdint.h>

extern int test_int(void);
extern int64_t test_long(void);
extern float test_float(void);

int
main(void)
{
	if(test_int() != 2)
		return 1;
	if(test_long() != 2)
		return 2;
	if(test_float() != 2.0f)
		return 3;
	return 0;
}
