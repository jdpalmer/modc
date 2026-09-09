int ok_if(void);
int ok_cmp(void);

int
main(void)
{
	if(ok_if() != 2)
		return 1;
	if(ok_cmp() != 1)
		return 2;
	return 0;
}
