int use_local(int, int);
int call_add(int, int);
int ptr_store(int *, int);

int
main(void)
{
	int x;

	if(use_local(20, 22) != 42)
		return 1;
	if(call_add(10, 32) != 42)
		return 2;
	x = 0;
	if(ptr_store(&x, 7) != 7)
		return 3;
	if(x != 7)
		return 4;
	return 0;
}
