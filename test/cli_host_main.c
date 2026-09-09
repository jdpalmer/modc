int host_ok(void);

int
main(void)
{
	if(host_ok() != 1)
		return 1;
	return 0;
}
