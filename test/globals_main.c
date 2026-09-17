int get_g(void);
void set_g(int);
int get_gi(void);
int bump_s(void);
int global_ptrs(void);
int first_char(void);
int str_len3(void);

int
main(void)
{
	if(get_gi() != 7)
		return 1;
	if(get_g() != 0)
		return 2;
	set_g(42);
	if(get_g() != 42)
		return 3;
	if(bump_s() != 1)
		return 4;
	if(bump_s() != 2)
		return 5;
	if(!global_ptrs())
		return 6;
	if(first_char() != 'h')
		return 7;
	if(str_len3() != 'a' + 'b' + 'c')
		return 8;
	return 0;
}
