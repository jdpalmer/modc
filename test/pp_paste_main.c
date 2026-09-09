int stringify_ident(void);
int stringify_no_expand(void);
int paste_ident(void);
int arg_expand(void);
int empty_paste(void);
int va_comma_paste(void);

int
main(void)
{
	if(!stringify_ident())
		return 1;
	if(!stringify_no_expand())
		return 2;
	if(paste_ident() != 42)
		return 3;
	if(arg_expand() != 42)
		return 4;
	if(empty_paste() != 7)
		return 5;
	if(!va_comma_paste())
		return 6;
	return 0;
}
