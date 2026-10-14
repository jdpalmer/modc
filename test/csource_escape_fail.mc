// fail: F:c_sources path must be relative

#pragma modc c_sources("../../../tmp/pwn.c")

int main() {
	return 0;
}
