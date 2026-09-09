int init_decl(void);
int init_assign(void);
int both_branches(int c);
int do_assigns(void);
int addr_escape(void);

int
main(void)
{
	if(init_decl() != 3)
		return 1;
	if(init_assign() != 4)
		return 2;
	if(both_branches(1) != 1 || both_branches(0) != 2)
		return 3;
	if(do_assigns() != 1)
		return 4;
	if(addr_escape() != 9)
		return 5;
	return 0;
}
