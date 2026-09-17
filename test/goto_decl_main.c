int forward_ok(int x);
int back_ok(int n);
int decl_before_goto(void);
int label_one(void);
int label_two(void);
int label_namespace(void);

int
main(void)
{
	if(forward_ok(2) != 2 || forward_ok(-1) != -1)
		return 1;
	if(back_ok(4) != 6)
		return 2;
	if(decl_before_goto() != 3)
		return 3;
	if(label_one() != 1 || label_two() != 2)
		return 4;
	if(label_namespace() != 3)
		return 5;
	return 0;
}
