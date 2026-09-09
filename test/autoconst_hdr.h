/* Header const retained as READONLY for Auto-const. */

static int
hdr_strlen(const char *s)
{
	int n;

	n = 0;
	if(s == 0)
		return 0;
	while(s[n])
		n++;
	return n;
}

static void
hdr_sink(char *p)
{
	(void)p;
}

static const char *
hdr_msg(void)
{
	return "hi";
}
