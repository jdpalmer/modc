int add(int, int);
int callfp(int (*)(int, int), int, int);
int callstar(int (*)(int, int), int, int);
int via_local(void);
int via_addr(void);
int choose(int, int, int);

int
main(void)
{
	if(callfp(add, 20, 22) != 42)
		return 1;
	if(callstar(add, 2, 3) != 5)
		return 2;
	if(via_local() != 42)
		return 3;
	if(via_addr() != 42)
		return 4;
	if(choose(1, 10, 7) != 17)
		return 5;
	if(choose(0, 10, 7) != 3)
		return 6;
	return 0;
}
