/* Method receivers: cannot rebind the receiver pointer. */
struct Buf {
	int n;
};

void (Buf* b).steal() {
	b = (Buf*)0;
}

int main() {
	return 0;
}
