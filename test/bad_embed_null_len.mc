typedef struct Buf {
	char[..];
}
Buf;

int bad(void) {
	return (int)len((Buf *)0);
}
