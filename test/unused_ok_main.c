int use_param(int x);
int use_local(void);
int main(void)
{
	if(use_param(0) != 1)
		return 1;
	if(use_local() != 1)
		return 1;
	return 0;
}
