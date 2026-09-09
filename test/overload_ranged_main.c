extern int test_fixed(void);
extern int test_literal(void);
extern int test_sizing(void);
extern int test_int_ranged(void);

int
main(void)
{
	if (test_fixed() != 0) {
		return 1;
	}
	if (test_literal() != 0) {
		return 2;
	}
	if (test_sizing() != 0) {
		return 3;
	}
	if (test_int_ranged() != 0) {
		return 4;
	}
	return 0;
}
