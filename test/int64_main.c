#include <stdint.h>

int64_t add64(int64_t, int64_t);
int int64_size(void);

int
main(void)
{
	if(int64_size() != 8)
		return 1;
	if(add64(20, 22) != 42)
		return 2;
	return 0;
}
