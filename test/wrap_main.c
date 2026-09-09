int wrap_add_max(void);
int wrap_mul(void);

int
main(void)
{
	int a;
	int m;

	a = wrap_add_max();
	m = wrap_mul();
	if(a != (int)0x80000000)
		return 1;
	if(m != (int)0x80000000)
		return 2;
	return 0;
}
