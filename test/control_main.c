int absdiff(int, int);
int sum_to(int);
int sum_pair(int);
int fact(int);
int countdown(int);
int early_break(int);
int skip_continue(int);
int land(int, int);
int lor(int, int);
int cond(int, int, int);
int max3(int, int, int);

int
main(void)
{
	if(absdiff(10, 3) != 7)
		return 1;
	if(absdiff(3, 10) != 7)
		return 2;
	if(sum_to(5) != 10)
		return 3;
	if(sum_pair(5) != 20)
		return 15;
	if(fact(5) != 120)
		return 4;
	if(countdown(4) != 10)
		return 5;
	if(early_break(20) != 10)
		return 6;
	if(skip_continue(5) != 8)
		return 7;
	if(land(1, 1) != 1)
		return 8;
	if(land(1, 0) != 0)
		return 9;
	if(lor(0, 0) != 0)
		return 10;
	if(lor(0, 1) != 1)
		return 11;
	if(cond(1, 4, 5) != 4)
		return 12;
	if(cond(0, 4, 5) != 5)
		return 13;
	if(max3(3, 9, 5) != 9)
		return 14;
	return 0;
}
