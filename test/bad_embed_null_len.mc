typedef struct Buf {
	char[..];
}
Buf;

int bad() {
	return (int)len((Buf *)0);
}
