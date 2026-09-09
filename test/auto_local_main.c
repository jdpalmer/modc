extern int infer_int(void);
extern double infer_double(void);
extern void *infer_ptr(void *);
extern int infer_array_decay(void);
extern int infer_ranged(void);
extern int infer_char_ptr(void);

int
main(void)
{
	int a[2];
	void *p;

	a[0] = 1;
	a[1] = 2;
	p = a;
	if(infer_int() != 3)
		return 1;
	if(infer_double() != 1.0)
		return 2;
	if(infer_ptr(p) != p)
		return 3;
	if(infer_array_decay() != 30)
		return 4;
	if(infer_ranged() != 6)
		return 5;
	if(infer_char_ptr() != (int)'h' + (int)'i')
		return 6;
	return 0;
}
