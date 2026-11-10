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

void use(int* p) {
	(void)p;
}

int* ok_use_then_defer() {
	Reg r = { &g };
	int* p = r.bump();
	use(p);
	defer r.reset();
	return &g;
}

int* ok_no_reset() {
	Reg r = { &g };
	use(r.bump());
	return &g;
}

int main() {
	(void)ok_use_then_defer();
	(void)ok_no_reset();
	return 0;
}
