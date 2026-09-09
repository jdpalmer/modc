extern int sum_array(void);
extern int sum_ranged_expr(void);
extern int sum_nested(void);
extern int explicit_type(void);
extern int with_break(void);
extern int ptr_bind_array(void);
extern int ptr_bind_ranged(void);
extern int ptr_bind_struct(void);

int
main(void)
{
	if(sum_array() != 10)
		return 1;
	if(sum_ranged_expr() != 18)
		return 2;
	if(sum_nested() != 10)
		return 3;
	if(explicit_type() != 30)
		return 4;
	if(with_break() != 3)
		return 5;
	if(ptr_bind_array() != 21)
		return 6;
	if(ptr_bind_ranged() != 14)
		return 7;
	if(ptr_bind_struct() != 14)
		return 8;
	return 0;
}
