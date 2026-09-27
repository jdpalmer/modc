// fail: F:implicit conversion from ranged array to ranged array requires a cast

int main() {
	const char[..] a = "hi";
	char[..] b = { 0 };
	b = a;
	return 0;
}
