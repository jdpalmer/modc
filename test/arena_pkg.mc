import "arena";

int test_copy_cat() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	a.init();
	defer a.free();
	{
		auto (copy_ok, s) = a.copy("hello world");
		ok = copy_ok;
		out = s;
	}
	if (!ok) {
		return 1;
	}
	if (cstr_write(buf, out) != 11) {
		return 2;
	}
	if (!str_eq("hello world", ranged(buf, cstr_zlen(buf)))) {
		return 3;
	}
	return 0;
}

int test_append() {
	Arena a = { 0 };
	char[..] s = { 0 };
	int i = { 0 };
	a.init();
	defer a.free();
	{
		auto (ok, next) = a.append(str_empty(), "hello");
		if (!ok) {
			return 1;
		}
		s = next;
	}
	{
		auto (ok, next) = a.append(s, " world");
		if (!ok) {
			return 2;
		}
		s = next;
	}
	if (!str_eq(s, "hello world")) {
		return 3;
	}
	if (len(s) != 11) {
		return 4;
	}
	{
		char[..] sub = { 0 };
		sub = s[0 .. 5];
		if (!str_eq(sub, "hello")) {
			return 5;
		}
		sub = s[6 ..];
		if (!str_eq(sub, "world")) {
			return 6;
		}
	}
	s = str_empty();
	if (len(s) != 0 || !str_eq(s, "")) {
		return 8;
	}
	if (a.z(s)[0] != '\0') {
		return 9;
	}
	for (i = 0; i < 100; i = i + 1) {
		auto (ok, next) = a.append_byte(s, 'a');
		if (!ok) {
			return 10;
		}
		s = next;
	}
	if (len(s) != 100) {
		return 11;
	}
	if (!str_starts_with(s, "aaa")) {
		return 12;
	}
	return 0;
}

int test_join() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	char[..] parts[3] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	parts[0] = "a";
	parts[1] = "b";
	parts[2] = "c";
	a.init();
	defer a.free();
	{
		auto (join_ok, next) = a.join(",", parts, 3);
		ok = join_ok;
		out = next;
	}
	if (!ok) {
		return 1;
	}
	if (!str_eq(out, "a,b,c")) {
		return 2;
	}
	if (cstr_write(buf, out) != 5) {
		return 3;
	}
	if (!str_eq(ranged(buf, cstr_zlen(buf)), "a,b,c")) {
		return 4;
	}
	parts[0] = "x";
	{
		auto (join_ok, next) = a.join(",", parts, 1);
		ok = join_ok;
		out = next;
	}
	if (!ok) {
		return 8;
	}
	if (!str_eq(out, "x")) {
		return 9;
	}
	parts[0] = "a";
	parts[1] = "b";
	parts[2] = "c";
	{
		auto (join_ok, next) = a.join(" | ", parts, 3);
		ok = join_ok;
		out = next;
	}
	if (!ok) {
		return 5;
	}
	if (!str_eq(out, "a | b | c")) {
		return 6;
	}
	{
		auto (join_ok, next) = a.join(",", parts, 0);
		ok = join_ok;
		out = next;
	}
	if (!ok) {
		return 7;
	}
	if (!str_is_empty(out)) {
		return 10;
	}
	return 0;
}

int test_replace() {
	Arena a = { 0 };
	char buf[32] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	a.init();
	defer a.free();
	{
		auto (rep_ok, next) = a.replace("foo bar foo", "foo", "baz");
		ok = rep_ok;
		out = next;
	}
	if (!ok) {
		return 1;
	}
	if (!str_eq(out, "baz bar baz")) {
		return 2;
	}
	if (cstr_write(buf, out) != 11) {
		return 3;
	}
	if (!str_eq(ranged(buf, cstr_zlen(buf)), "baz bar baz")) {
		return 4;
	}
	{
		auto (rep_ok, next) = a.replace("x-x-x", "-", "+");
		ok = rep_ok;
		out = next;
	}
	if (!ok) {
		return 5;
	}
	if (!str_eq(out, "x+x+x")) {
		return 6;
	}
	{
		auto (rep_ok, next) = a.replace("abc", "z", "w");
		ok = rep_ok;
		out = next;
	}
	if (!ok) {
		return 7;
	}
	if (!str_eq(out, "abc")) {
		return 8;
	}
	return 0;
}

int test_reset() {
	Arena a = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	a.init();
	defer a.free();
	{
		auto (copy_ok, next) = a.copy("first");
		ok = copy_ok;
		out = next;
	}
	if (!ok || !str_eq(out, "first")) {
		return 1;
	}
	a.reset();
	{
		auto (copy_ok, next) = a.copy("second");
		ok = copy_ok;
		out = next;
	}
	if (!ok || !str_eq(out, "second")) {
		return 2;
	}
	return 0;
}

int arena_pkg_run() {
	if (test_copy_cat() != 0) {
		return 1;
	}
	if (test_append() != 0) {
		return 2;
	}
	if (test_join() != 0) {
		return 3;
	}
	if (test_replace() != 0) {
		return 4;
	}
	if (test_reset() != 0) {
		return 5;
	}
	return 0;
}
