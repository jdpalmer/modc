/* auto for initialized locals: type inferred from initializer (with decay). */

int infer_int(void) {
	auto n = 3;
	return n;
}

double infer_double(void) {
	auto q = 1.0;
	return q;
}

void* infer_ptr(void* p) {
	auto r = p;
	return r;
}

int infer_array_decay(void) {
	int a[4] = {
		0 };
	auto p = a;
	a[0] = 10;
	a[1] = 20;
	return p[0] + p[1];
}

int infer_ranged(void) {
	int a[3] = {
		0 };
	int[..] s = {
		0 };
	int sum = {
		0 };
	int i = {
		0 };
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	s = a;
	sum = 0;
	for (i = 0; i < (int)len(s); i++) {
		sum = sum + s[i];
	}
	return sum;
}

int infer_char_ptr(void) {
	auto msg = "hi";
	return (int)msg[0] + (int)msg[1];
}
