int sum(int, ...);
int sum_copy(int, ...);
int forward(int, ...);
int call_sum(void);

int
main(void)
{
	if(sum(3, 10, 20, 12) != 42)
		return 1;
	if(sum(0) != 0)
		return 2;
	if(sum_copy(3, 1, 2, 3) != 6)
		return 3;
	if(forward(3, 10, 20, 12) != 42)
		return 4;
	if(call_sum() != 42)
		return 5;
	return 0;
}
