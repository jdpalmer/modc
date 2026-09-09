int green_is_one(void);
int mix_ok_via_int(void);
int as_int(int); /* Color lowered as int in ABI */

int
main(void)
{
	if(!green_is_one())
		return 1;
	if(!mix_ok_via_int())
		return 2;
	if(as_int(2) != 2)
		return 3;
	return 0;
}
