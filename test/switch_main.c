int classify(int);
int case_fall(int);
int case_range(int);
int withbreak(int);
int skipto(int);
int loop_goto(int);

int
main(void)
{
	if(classify(1) != 10)
		return 1;
	if(classify(2) != 20)
		return 2;
	if(classify(3) != 30)
		return 3;
	if(classify(4) != 30)
		return 4;
	if(classify(9) != 0)
		return 5;
	if(case_fall(1) != 3)
		return 6;
	if(case_fall(2) != 2)
		return 7;
	if(case_fall(3) != 3)
		return 8;
	if(skipto(5) != 6)
		return 9;
	if(skipto(0) != 100)
		return 10;
	if(skipto(-3) != -1)
		return 11;
	if(loop_goto(5) != 10)
		return 12;
	if(withbreak(0) != 3)
		return 13;
	if(case_range(1) != 100 || case_range(2) != 100 || case_range(3) != 100)
		return 14;
	if(case_range(4) != 0)
		return 15;
	if(case_range(5) != 50)
		return 16;
	if(case_range(10) != 200 || case_range(12) != 200)
		return 17;
	if(case_range(11) != 200)
		return 18;
	return 0;
}
