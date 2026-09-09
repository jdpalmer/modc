int sum_buf_local(void);
int sum_triple(void);

int
main(void)
{
	if(sum_buf_local() != 10)
		return 1;
	if(sum_triple() != 60)
		return 2;
	return 0;
}
