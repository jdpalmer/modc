import "arena";

int bad(void) {
	char[..] s = {0};
	s = ((U8 *)0)[0 .. 5];
	return 0;
}
