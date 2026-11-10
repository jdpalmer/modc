// fail: F:cannot escape

char[..] bad_pool_view() {
	pool p {
		char[..] s = { 0 };
		(void)p.copy(&s, "hi");
		return s;
	}
}

int* bad_pool_alloc_ptr() {
	pool p {
		int* n = p.alloc(int);
		return n;
	}
}

int[..] bad_pool_alloc_slice() {
	pool p {
		int[..] s = p.alloc(int, 2);
		return s;
	}
}

int main() {
	return 0;
}
