int conv_array(void);
int conv_string(void);

int
main(void)
{
	if(conv_array() != 10)
		return 1;
	if(conv_string() != 2)
		return 2;
	return 0;
}
