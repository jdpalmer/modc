/* Portable test (was fixed_array_param.mc + fixed_array_param_main.c). */
#include <assert.h>

int sum8(int a[8]) {
	int i = { 0 };
	int t = { 0 };
	t = 0;
	for (i = 0; i < (int)len(a); i++) {
		t = t + a[i];
	}
	return t;
}

int fixed_param_run() {
	int a[8] = { 0 };
	int i = { 0 };
	for (i = 0; i < 8; i++) {
		a[i] = i + 1;
	}
	return sum8(a);
}

int main() {
	return fixed_param_run() != 36;
}
