/* Null method receiver with runtime checks (default); must abort (non-zero). */

struct T {
	int x;
};

int (T* t).get() {
	return t.x;
}

int main() {
	T* p = 0;
	return p.get();
}
