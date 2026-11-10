// fail: F:union must not contain struct or union members

struct Point {
	int x;
	int y;
};

union Bad {
	int tag;
	Point p;
};

int main() {
	return 0;
}
