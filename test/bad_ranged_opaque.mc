int f() {
	char buf[2] = { 0 };
	char[..] s = { 0 };
	buf[0] = 'h';
	buf[1] = 'i';
	s = ranged(buf, 2);
	return (int)s.len;
}
