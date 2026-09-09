struct P {
	int x;
	int y;
};

struct Rect {
	struct P min;
	struct P max;
};

int dot_x(void);
int dot_y(void);
int rect_min_x(void);
int rect_max_y(void);
int arr0(void);
int arr5(void);
int sparse1(void);
int sparse3(void);

int
main(void)
{
	if(dot_x() != 10 || dot_y() != 20)
		return 1;
	if(rect_min_x() != 1 || rect_max_y() != 6)
		return 2;
	if(arr0() != 1 || arr5() != 2)
		return 3;
	if(sparse1() != 7 || sparse3() != 9)
		return 4;
	return 0;
}
