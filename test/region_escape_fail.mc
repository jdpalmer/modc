// fail: F:cannot escape

int g = 0;

struct Reg {
	int* base;
};

int* ([[region]] Reg* r).bump() {
	return r.base;
}

void ([[releases]] Reg* r).reset() {
	(void)r;
}

int* bad_defer() {
	Reg r = { &g };
	defer r.reset();
	return r.bump();
}

int* bad_taint() {
	Reg r = { &g };
	defer r.reset();
	int* p = r.bump();
	return p;
}

int* bad_after_reset() {
	Reg r = { &g };
	int* p = r.bump();
	r.reset();
	return p;
}

int main() {
	return 0;
}
