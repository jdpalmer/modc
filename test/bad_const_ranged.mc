int main() {
	const char[..] a = "hi";
	char[..] b = { 0 };
	b = a;
	return 0;
}
