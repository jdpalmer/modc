// fail: F:%C requires braces around while body

int bad_while(int n) {
	while (n > 0)n--;
	return n;
}
