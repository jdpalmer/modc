/* Host ABI long — allowed in headers */
static long header_long_add(long a, long b)
{
	return a + b;
}

static int
header_long_size(void)
{
	return (int)sizeof(long);
}
