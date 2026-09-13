typedef struct Buf {
	char[..];
}
Buf;

int bad(void) {
	char[..] s = {
		0 };
	s = ((Buf *)0)[0 .. 5];
	return 0;
}
