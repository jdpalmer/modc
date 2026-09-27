// fail: F:C99 designated initializers (.field =)

struct P {
	int x;
	int y;
};

struct P g = {
	y: 1, x: 2 };
