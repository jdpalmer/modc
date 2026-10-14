// fail: F:invalid c_libs name

#pragma modc c_libs("-lm")

int main() {
	return 0;
}
