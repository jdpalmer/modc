// ok:

struct Reg {
	int* base;
};

void ([[releases]] Reg* r).reset() {
	(void)r;
}

void use(Reg* r) {
	(void)r;
}

int main() {
	int g = 0;
	Reg r = { &g };
	use(&r);
	r.reset();
	return 0;
}
