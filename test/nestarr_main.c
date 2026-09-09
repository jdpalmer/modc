int get22(void);
int sum2d(int[2][3]);

int
main(void)
{
	int a[2][3];
	int i;
	int j;
	int n;

	if(get22() != 6)
		return 1;
	n = 1;
	for(i = 0; i < 2; i++)
		for(j = 0; j < 3; j++) {
			a[i][j] = n;
			n++;
		}
	if(sum2d(a) != 21)
		return 2;
	return 0;
}
