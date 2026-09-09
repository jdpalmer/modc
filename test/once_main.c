int once_value(void);

int
main(void)
{
	return once_value() == 99 ? 0 : 1;
}
