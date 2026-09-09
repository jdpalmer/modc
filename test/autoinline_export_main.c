int autoinline_export_run(void);
int add(int a, int b);
int
main(void)
{
	if(autoinline_export_run() != 0) {
		return 1;
	}
	/* Out-of-line export still linkable from C. */
	if(add(1, 2) != 3) {
		return 2;
	}
	return 0;
}
