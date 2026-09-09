int add(int, int);
int arithmetic(int, int);

int
main(void)
{
	if(add(20, 22) != 42)
		return 1;
	if(arithmetic(6, 7) != 40)
		return 2;
	return 0;
}
