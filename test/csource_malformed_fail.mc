// fail: F:invalid token in c_sources path

#pragma modc c_sources(shim + broken.c)

int main() {
	return 0;
}
