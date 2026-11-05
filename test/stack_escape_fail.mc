// fail: F:address of local 'x' escapes this function
// fail: F:address of local array 'a' escapes this function
// fail: F:address of parameter 's' escapes this function
// fail: F:stack address held in 'p' escapes this function
// fail: F:stack address held in 'h' escapes this function
// fail: F:passing stack address to [[retained]] parameter

#include <stddef.h>
#include <stdbool.h>

int* g;

struct Box {
	int n;
};

struct Holder {
	int* p;
};

int* bad_local() {
	int x = 0;
	return &x;
}

int* bad_array() {
	int a[4] = { 0 };
	return a;
}

int[..] bad_subrange() {
	int a[4] = { 0 };
	return a[1..3];
}

int[..] bad_ranged() {
	int a[4] = { 0 };
	return ranged(a, 4);
}

(int*, bool) bad_tuple() {
	int x = 0;
	return (&x, true);
}

void bad_global_store() {
	int x = 0;
	g = &x;
}

void bad_out_store(int** out) {
	int x = 0;
	*out = &x;
}

Box* bad_param_struct(Box s) {
	return &s;
}

int* bad_taint() {
	int x = 0;
	int* p = &x;
	return p;
}

Holder bad_agg_taint() {
	int x = 0;
	Holder h = { 0 };
	h.p = &x;
	return h;
}

void stash([[retained]] int* p) {
	g = p;
}

void bad_retained_call() {
	int x = 0;
	stash(&x);
}

int* id_returns(int* p) {
	return p;
}

void bad_return_flow() {
	int x = 0;
	g = id_returns(&x);
}

void stash_infer(int* p) {
	g = p;
}

void bad_inferred_retain() {
	int x = 0;
	stash_infer(&x);
}

int main() {
	return 0;
}
