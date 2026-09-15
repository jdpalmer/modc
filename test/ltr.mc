/* Operand evaluation is left-to-right. */

int ltr_assign_add(void) {
	int a = { 0 };
	int b = { 0 };
	a = 0;
	b = 0;
	/* Left (a = 1) completes before right (b = a), so b is 1; result 2. */
	return (a = 1) + (b = a);
}
