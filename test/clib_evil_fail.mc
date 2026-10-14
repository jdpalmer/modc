// fail: F:invalid c_libs name

#pragma modc c_libs(":../../evil.so")

int main() {
	return 0;
}
