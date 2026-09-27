/* Portable test (was for_init_decl.mc + for_init_decl_main.c). */
#include <assert.h>

/* C99 for-init declarations: for (int i = 0; …) */

int sum_to(int n) {
	int s = { 0 };
	s = 0;
	for (int i = 0; i < n; i++) {
		s = s + i;
	}
	return s;
}

int sum_auto(int n) {
	int s = { 0 };
	s = 0;
	for (auto i = 0; i < n; i++) {
		s = s + i;
	}
	return s;
}

int nested_scopes(int n) {
	int s = { 0 };
	s = 0;
	for (int i = 0; i < n; i++) {
		for (int j = 0; j <= i; j++) {
			s = s + 1;
		}
	}
	return s;
}

int main() {
	assert_eq(sum_to(5), 10);
	assert_eq(sum_auto(5), 10);
	assert_eq(nested_scopes(4), 10);
	return 0;
}
