// fail: F:[[provides]] requires a pointer, T[..], aggregate, or tuple return

[[provides]] int bad() {
	return 0;
}

int main() {
	return 0;
}
