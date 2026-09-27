// fail: flags: -M $ROOT
// fail: F:cannot use null pointer to Buf

typedef struct Buf {
	char[..];
}
Buf;

int bad() {
	return (int)len((Buf *)0);
}
