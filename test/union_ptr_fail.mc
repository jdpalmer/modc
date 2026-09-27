// fail: F:union must not mix pointers with non-pointer members in %C user code (allowed in headers)

union Bad {
	void* p;
	int x;
};

int main() {
	return 0;
}
