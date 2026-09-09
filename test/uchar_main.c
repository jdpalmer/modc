int hi_byte_eq(void);
int hi_byte_as_int(void);
int uchar_alias(void);

int
main(void)
{
	if(!hi_byte_eq())
		return 1;
	if(hi_byte_as_int() != 255)
		return 2;
	if(!uchar_alias())
		return 3;
	return 0;
}
