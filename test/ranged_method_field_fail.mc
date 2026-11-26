// fail: F:method name len conflicts with field on T[..]

int (int[..] a).len() {
	return (int)len(a);
}

int main() {
	return 0;
}
