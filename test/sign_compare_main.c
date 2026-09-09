int both_signed(int a, int b);
int both_unsigned(unsigned a, unsigned b);
int sizeof_cast(int i);
int eq_mixed_ok(int i, unsigned u);

int
main(void)
{
	if(!both_signed(1, 2))
		return 1;
	if(!both_unsigned(1, 2))
		return 2;
	if(!sizeof_cast(3))
		return 3;
	if(!eq_mixed_ok(-1, (unsigned)-1))
		return 4;
	return 0;
}
