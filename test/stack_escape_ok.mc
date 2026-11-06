#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

int g_x;
int* g_p;

struct Box {
	int n;
};

struct Box g_box;

struct Screen {
	int n;
	int* cells;
};

static int* static_slot() {
	static int x = 0;
	return &x;
}

int* ret_global() {
	return &g_x;
}

int* ret_static_local() {
	return static_slot();
}

int* ret_param_ptr(int* p) {
	return p;
}

int* ret_heap() {
	int* p = malloc(sizeof(int));
	return p;
}

int* ret_through_ptr(Screen* s) {
	return s.cells;
}

int* ret_global_field() {
	return &g_box.n;
}

void use_local_addr(int* p) {
	(void)p;
}

void borrow_ok(int* p) {
	*p = *p + 1;
}

void (Box* b).bump() {
	b.n = b.n + 1;
}

int ok_local_use() {
	int x = 0;
	int* p = &x;
	Box box = { 0 };
	char buf[32] = { 0 };
	*p = 1;
	use_local_addr(&x);
	borrow_ok(&x);
	box.bump();
	g_p = &g_x;
	(void)ret_param_ptr(&x);
	snprintf(buf, sizeof(buf), "%d", x);
	(void)strlen(buf);
	memset(buf, 0, sizeof(buf));
	return x;
}

int main() {
	return ok_local_use() == 1 ? 0 : 1;
}
