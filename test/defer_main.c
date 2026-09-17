extern int defer_log[16];
extern int nlog;
extern int ret_order_stamp;

int lifo(void);
int on_return(void);
int on_block_end(void);
int on_break(void);
int on_goto(void);
int on_branch(int x);
int with_ranged(void);
int ret_order(void);
int ret_order_tuple_ok(void);

int
main(void)
{
	nlog = 0;
	if(lifo() != 0)
		return 1;
	if(nlog != 4 || defer_log[0] != 0 || defer_log[1] != 1 || defer_log[2] != 2 || defer_log[3] != 3)
		return 2;

	nlog = 0;
	if(on_return() != 42)
		return 3;
	if(nlog != 1 || defer_log[0] != 100)
		return 4;

	nlog = 0;
	if(on_block_end() != 0)
		return 5;
	if(nlog != 5 || defer_log[0] != 0 || defer_log[1] != 10 || defer_log[2] != 1 || defer_log[3] != 2 || defer_log[4] != 20)
		return 6;

	if(on_break() != 0)
		return 7;
	if(nlog != 1 || defer_log[0] != 1)
		return 8;

	if(on_goto() != 0)
		return 9;
	if(nlog != 1 || defer_log[0] != 5)
		return 10;

	nlog = 0;
	if(on_branch(1) != 1)
		return 11;
	if(nlog != 2 || defer_log[0] != 8 || defer_log[1] != 9)
		return 12;

	nlog = 0;
	if(on_branch(0) != 2)
		return 13;
	if(nlog != 1 || defer_log[0] != 9)
		return 14;

	if(with_ranged() != 6)
		return 15;
	if(nlog != 1 || defer_log[0] != 99)
		return 16;

	ret_order_stamp = 0;
	if(ret_order() != 1)
		return 17;
	if(ret_order_stamp != 2)
		return 18;

	if(ret_order_tuple_ok() != 0)
		return 19;

	return 0;
}
