int empty_parens(void);
int explicit_void(void);
int one_arg(int x);
int call_ok(void);

int
main(void)
{
	if(empty_parens() != 1)
		return 1;
	if(explicit_void() != 1)
		return 2;
	if(call_ok() != 42)
		return 3;
	return 0;
}
