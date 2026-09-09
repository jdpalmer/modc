int flag_ok(void);
int value_ok(void);

int
main(void)
{
	if(flag_ok() != 1)
		return 1;
	if(value_ok() != 7)
		return 2;
	return 0;
}
