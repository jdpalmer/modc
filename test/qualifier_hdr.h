/* Headers may use const / inline / volatile / restrict / register. */

static inline int
hdr_const_len(const char *s)
{
	int n;

	n = 0;
	if(s == 0)
		return 0;
	while(s[n])
		n++;
	return n;
}

int
hdr_qual_ok(void)
{
	volatile int v;
	register int r;
	int *restrict p;

	v = 3;
	r = hdr_const_len("ab");
	p = 0;
	(void)p;
	return r + v;
}
