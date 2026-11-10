// fail: F:use of consumed value

struct Reg {
	int* base;
};

void ([[consumes]] Reg* r).reset() {
	(void)r;
}

void use(Reg* r) {
	(void)r;
}

void bad_double_reset() {
	int g = 0;
	Reg r = { &g };
	r.reset();
	r.reset();
}

void bad_use_after_reset() {
	int g = 0;
	Reg r = { &g };
	r.reset();
	use(&r);
}

void bad_field_after_reset() {
	int g = 0;
	Reg r = { &g };
	r.reset();
	r.base = 0;
}

void take([[consumes]] Reg* r) {
	(void)r;
}

void bad_double_take_addr() {
	int g = 0;
	Reg r = { &g };
	take(&r);
	take(&r);
}

int main() {
	return 0;
}
