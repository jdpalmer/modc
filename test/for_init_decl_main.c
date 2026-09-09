extern int sum_to(int n);
extern int sum_auto(int n);
extern int nested_scopes(int n);

int
main(void)
{
	if(sum_to(5) != 10)
		return 1;
	if(sum_auto(5) != 10)
		return 2;
	if(nested_scopes(4) != 10)
		return 3;
	return 0;
}
