/* Portable test (was switch.mc + switch_main.c). */
#include <assert.h>

/* switch, goto, labels, and fallthrough; */

int classify(int x) {
	switch (x) {
		case 1: return 10;
		case 2: return 20;
		case 3: case 4: return 30;
		default: return 0;
	}
}

int case_fall(int x) {
	int s = { 0 };
	s = 0;
	switch (x) {
		case 1: s = s + 1;
		fallthrough;
		case 2: s = s + 2;
		break;
		case 3: s = 3;
		break;
	}
	return s;
}

int case_range(int x) {
	switch (x) {
		case 1 .. 3: return 100;
		case 10 .. 12: return 200;
		case 5: return 50;
		default: return 0;
	}
}

int withbreak(int x) {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	for (i = 0; i < 10; i++) {
		switch (i) {
			case 3: break;
			default: s = s + i;
			continue;
		}
		break;
	}
	return s + x;
}

int skipto(int x) {
	if (x < 0) {
		goto neg;
	}
	if (x == 0) {
		goto zero;
	}
	return x + 1;
	zero: return 100;
	neg: return -1;
}

int loop_goto(int n) {
	int i = { 0 };
	int s = { 0 };
	s = 0;
	i = 0;
	again: if (i >= n) {
		goto done;
	}
	s = s + i;
	i++;
	goto again;
	done: return s;
}

int main() {
	assert_eq(classify(1), 10);

	assert_eq(classify(2), 20);

	assert_eq(classify(3), 30);

	assert_eq(classify(4), 30);

	assert_eq(classify(9), 0);

	assert_eq(case_fall(1), 3);

	assert_eq(case_fall(2), 2);

	assert_eq(case_fall(3), 3);

	assert_eq(skipto(5), 6);

	assert_eq(skipto(0), 100);

	assert_eq(skipto(-3), -1);

	assert_eq(loop_goto(5), 10);

	assert_eq(withbreak(0), 3);

	if (case_range(1) != 100 || case_range(2) != 100 || case_range(3) != 100) {

		return 14;

	}

	assert_eq(case_range(4), 0);

	assert_eq(case_range(5), 50);

	if (case_range(10) != 200 || case_range(12) != 200) {

		return 17;

	}

	assert_eq(case_range(11), 200);

	return 0;
}
