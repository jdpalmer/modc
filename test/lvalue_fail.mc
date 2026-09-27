// fail: F:left operand of assignment is not a modifiable lvalue
// fail: F:operator '++' requires a modifiable lvalue
// fail: F:address-of requires an lvalue or function

void bad() {
	1 = 2;
	++3;
	4++;
	&(5);
}
