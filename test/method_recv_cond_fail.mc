// fail: F:cannot test method receiver 'b' for null; callers must pass a live object

/* Method receivers: bare if (recv) is a null test. */
struct Buf {
	int n;
};

void (Buf* b).maybe() {
	if (b) {
		b.n = 1;
	}
}

int main() {
	return 0;
}
