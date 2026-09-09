int strlen_local(void);
int strlen_temp(void);
int pass_ranged(void);
int mutable_ok(void);

int
main(void)
{
	if(strlen_local() != 1)
		return 1;
	if(strlen_temp() != 5)
		return 2;
	if(pass_ranged() != 1)
		return 3;
	if(mutable_ok() != 1)
		return 4;
	return 0;
}
