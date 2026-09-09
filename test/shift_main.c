#include <stdint.h>

int shl_ok(int x);
int shr_ok(int x);
int64_t shl_long(int64_t x);
int shl_assign(int x);
int shl_var(int x, int n);
int shl_var_wrap(int x);

int
main(void)
{
	if(shl_ok(1) != 8)
		return 1;
	if(shr_ok(16) != 4)
		return 2;
	if(shl_long(1) != ((int64_t)1 << 40))
		return 3;
	if(shl_assign(1) != 16)
		return 4;
	if(shl_var(1, 3) != 8)
		return 5;
	/* 32 & 31 == 0 → shift by 0 */
	if(shl_var_wrap(7) != 7)
		return 6;
	if(shl_var(1, 32) != 1)
		return 7;
	return 0;
}
