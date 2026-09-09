int from_include(void);
int ifndef_ok(void);
int macro_add(int, int);
int local_def(void);
int after_undef(void);

int
main(void)
{
	if(from_include() != 42)
		return 1;
	if(ifndef_ok() != 1)
		return 2;
	if(macro_add(20, 22) != 42)
		return 3;
	if(local_def() != 42)
		return 4;
	if(after_undef() != 7)
		return 5;
	return 0;
}
