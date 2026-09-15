import "str";

int test_cstr_write(void) {
	char buf[32] = { 0 };
	char[..] a = { 0 };
	a = "hello";
	if (cstr_write(buf, a) != 5) {
		return 1;
	}
	if (!str_eq_cstr(a, buf)) {
		return 2;
	}
	return 0;
}

int test_find(void) {
	char[..] hay = { 0 };
	char[..] needle = { 0 };
	hay = "hello world";
	needle = "world";
	{
		auto (ok, hit) = str_find(hay, needle);
		if (!ok || !str_eq(hit, needle)) {
			return 1;
		}
	}
	{
		auto (ok, hit) = str_ifind(hay, "LO");
		if (!ok || !str_eq(hit, "lo")) {
			return 2;
		}
	}
	{
		auto (ok, hit) = str_rfind(hay, "l");
		if (!ok || !str_eq(hit, "l")) {
			return 3;
		}
	}
	{
		auto (ok, miss) = str_find(hay, "xyz");
		(void)miss;
		if (ok) {
			return 4;
		}
	}
	return 0;
}

int test_chomp(void) {
	char raw[8] = { 0 };
	char[..] s = { 0 };
	char[..] tok = { 0 };
	raw[0] = (char)'l';
	raw[1] = (char)'i';
	raw[2] = (char)'n';
	raw[3] = (char)'e';
	raw[4] = (char)'\n';
	raw[5] = (char)0;
	s = ranged(raw, 5);
	tok = str_chomp(s);
	if (!str_eq(tok, "line")) {
		return 1;
	}
	return 0;
}

int test_sep_trim(void) {
	char line[64] = { 0 };
	char[..] rest = { 0 };
	char[..] tok = { 0 };
	int n = { 0 };
	cstr_write(line, "  foo, bar , baz  ");
	rest = str_from_cstr(line);
	{
		auto (t, r) = str_split_once(rest, " ");
		tok = t;
		rest = r;
	}
	while (str_is_empty(tok) && !str_is_empty(rest)) {
		auto (t, r) = str_split_once(rest, " ");
		tok = t;
		rest = r;
	}
	if (!str_eq(tok, "foo,")) {
		return 1;
	}
	tok = str_trim("  hi  ");
	if (!str_eq(tok, "hi")) {
		return 2;
	}
	if (test_chomp() != 0) {
		return 3;
	}
	cstr_write(line, "a=b=c");
	rest = str_from_cstr(line);
	n = 0;
	while (!str_is_empty(rest)) {
		auto (t, r) = str_split_once(rest, "=");
		tok = t;
		rest = r;
		n = n + 1;
		if (n == 1 && !str_eq(tok, "a")) {
			return 4;
		}
		if (n == 2 && !str_eq(tok, "b")) {
			return 5;
		}
		if (n == 3 && !str_eq(tok, "c")) {
			return 6;
		}
	}
	if (n != 3) {
		return 7;
	}
	return 0;
}

int test_cmp_prefix(void) {
	char[..] s = { 0 };
	s = "Hello";
	if (!str_starts_with(s, "He")) {
		return 1;
	}
	if (!str_ends_with(s, "lo")) {
		return 2;
	}
	if (str_cmp(s, "Hello") != 0) {
		return 3;
	}
	if (str_icmp(s, "hello") != 0) {
		return 4;
	}
	return 0;
}

int test_parse(void) {
	{
		auto (ok, v) = str_to_long("12345", 10);
		if (!ok || v != 12345) {
			return 1;
		}
	}
	{
		auto (ok, v) = str_to_long("-99", 10);
		if (!ok || v != -99) {
			return 2;
		}
	}
	{
		auto (ok, badv) = str_to_long("gg", 10);
		(void)badv;
		if (ok) {
			return 3;
		}
	}
	return 0;
}

int test_subview(void) {
	char data[16] = { 0 };
	char[..] chunk = { 0 };
	char[..] word = { 0 };
	data[0] = (char)'h';
	data[1] = (char)'i';
	data[2] = (char)'!';
	chunk = ranged(data, 3);
	word = chunk[0 .. 2];
	if (!str_eq(word, "hi")) {
		return 1;
	}
	return 0;
}

int str_pkg_run(void) {
	if (test_cstr_write() != 0) {
		return 1;
	}
	if (test_find() != 0) {
		return 2;
	}
	if (test_sep_trim() != 0) {
		return 3;
	}
	if (test_cmp_prefix() != 0) {
		return 4;
	}
	if (test_parse() != 0) {
		return 5;
	}
	if (test_subview() != 0) {
		return 6;
	}
	return 0;
}
