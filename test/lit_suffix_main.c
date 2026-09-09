int suf_short(void);
int suf_ushort(void);
int suf_uint(void);
int suf_long(void);
int suf_ulong(void);
int suf_lu(void);
int suf_case(void);
int suf_float(void);
int suf_int_f(void);
int suf_bin_u(void);
int suf_types(void);

int
main(void)
{
	if(suf_short())
		return 1;
	if(suf_ushort())
		return 2;
	if(suf_uint())
		return 3;
	if(suf_long())
		return 4;
	if(suf_ulong())
		return 5;
	if(suf_lu())
		return 6;
	if(suf_case())
		return 7;
	if(suf_float())
		return 8;
	if(suf_int_f())
		return 9;
	if(suf_bin_u())
		return 10;
	if(suf_types())
		return 11;
	return 0;
}
