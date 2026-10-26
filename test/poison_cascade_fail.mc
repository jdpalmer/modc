// fail: F:undeclared identifier no_such
// fail: F:no field named nope
// fail: !F:invalid operands to binary operator
// fail: !F:called object is not a function
// fail: !F:array subscript must be an integer
// TyError poison: primary errors only; no cascade from uses of bad exprs.

struct S {
	int x;
};

int f(int a) {
	return a;
}

int main() {
	struct S s = { 0 };
	int x = no_such + 1;
	x = no_such * 2;
	x = f(no_such);
	x = no_such[0];
	x = s.nope + 1;
	return x;
}
