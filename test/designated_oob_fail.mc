// fail: F:array designator index 2 is outside array bound 2

int a[2] = {
	[2] = 1 };

int main() {
	return a[0];
}
