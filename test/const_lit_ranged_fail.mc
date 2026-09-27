// fail: F:implicit conversion from ranged array to ranged array requires a cast

int main() {
	char[..] s = "hello";
	return 0;
}
