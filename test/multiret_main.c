extern int use_auto(void);
extern int use_explicit(void);
extern int use_maybe_ok(void);
extern int use_maybe_fail(void);
extern int use_blank_ok(void);
extern int use_blank_both(void);
extern int use_blank_explicit(void);

int
main(void)
{
	if(use_auto() != 3)
		return 1;
	if(use_explicit() != 3)
		return 2;
	if(use_maybe_ok() != 5)
		return 3;
	if(use_maybe_fail() != 0)
		return 4;
	if(use_blank_ok() != 7)
		return 5;
	if(use_blank_both() != 0)
		return 6;
	if(use_blank_explicit() != 4)
		return 7;
	return 0;
}
