int sizeof_local(void);
int sizeof_ptr(int *p);
int sizeof_elem(int a[8]);

int
main(void)
{
	int x;

	x = 0;
	if(sizeof_local() != 16)
		return 1;
	if(sizeof_ptr(&x) != 8)
		return 2;
	if(sizeof_elem(&x) != 4)
		return 3;
	return 0;
}
