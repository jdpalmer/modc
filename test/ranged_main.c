int from_array(void);
int from_ptr(void);
int from_scratch(void);
int index_write(void);
int roundtrip(void);
int ranged_size(void);
int from_string(void);
int len_fixed(void);
int len_ranged(void);
int cap_ranged(void);

int
main(void)
{
	if(from_array() != 10)
		return 1;
	if(from_ptr() != 60)
		return 2;
	if(index_write() != 15)
		return 3;
	if(roundtrip() != 9)
		return 4;
	if(ranged_size() != 24)
		return 5;
	if(!from_string())
		return 6;
	if(len_fixed() != 5)
		return 7;
	if(len_ranged() != 3)
		return 8;
	if(from_scratch() != 3)
		return 9;
	if(cap_ranged() != 3)
		return 10;
	return 0;
}
