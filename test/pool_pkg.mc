import "str";

int test_copy_cat() {
	char buf[32] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	pool p {
		ok = p.copy(&out, "hello world");
		if (!ok) {
			return 1;
		}
		if (cstr_write(buf, out) != 11) {
			return 2;
		}
		if (!str_eq("hello world", ranged(buf, cstr_zlen(buf)))) {
			return 3;
		}
	}
	return 0;
}

int test_append() {
	char[..] s = { 0 };
	int i = { 0 };
	pool p {
		if (!p.copy(&s, "hello")) {
			return 1;
		}
		if (!p.append(&s, " world")) {
			return 2;
		}
		if (!str_eq(s, "hello world")) {
			return 3;
		}
		if (len(s) != 11) {
			return 4;
		}
		{
			const char[..] sub = s[0 .. 5];
			if (!str_eq(sub, "hello")) {
				return 5;
			}
			if (!str_eq(s[6 ..], "world")) {
				return 6;
			}
		}
		s = str_empty();
		if (len(s) != 0 || !str_eq(s, "")) {
			return 8;
		}
		{
			char* z = p.cstr(s);
			if (z == 0 || *z != '\0') {
				return 9;
			}
		}
		for (i = 0; i < 100; i = i + 1) {
			if (!p.push(&s, 'a')) {
				return 10;
			}
		}
		if (len(s) != 100) {
			return 11;
		}
		if (!str_starts_with(s, "aaa")) {
			return 12;
		}
	}
	return 0;
}

int test_join() {
	char buf[32] = { 0 };
	const char[..] parts[3] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	parts[0] = "a";
	parts[1] = "b";
	parts[2] = "c";
	pool p {
		ok = p.join(&out, ",", parts, 3);
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
		ok = p.join(&out, ",", parts, 1);
		if (!ok) {
			return 8;
		}
		if (!str_eq(out, "x")) {
			return 9;
		}
		parts[0] = "a";
		parts[1] = "b";
		parts[2] = "c";
		ok = p.join(&out, " | ", parts, 3);
		if (!ok) {
			return 5;
		}
		if (!str_eq(out, "a | b | c")) {
			return 6;
		}
		ok = p.join(&out, ",", parts, 0);
		if (!ok) {
			return 7;
		}
		if (!str_is_empty(out)) {
			return 10;
		}
	}
	return 0;
}

int test_replace() {
	char buf[32] = { 0 };
	char[..] out = { 0 };
	bool ok = { 0 };
	pool p {
		ok = p.replace(&out, "foo bar foo", "foo", "baz");
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
		ok = p.replace(&out, "x-x-x", "-", "+");
		if (!ok) {
			return 5;
		}
		if (!str_eq(out, "x+x+x")) {
			return 6;
		}
		ok = p.replace(&out, "abc", "z", "w");
		if (!ok) {
			return 7;
		}
		if (!str_eq(out, "abc")) {
			return 8;
		}
	}
	return 0;
}

int test_reset() {
	char[..] out = { 0 };
	bool ok = { 0 };
	pool p {
		ok = p.copy(&out, "first");
		if (!ok || !str_eq(out, "first")) {
			return 1;
		}
		p.reset();
		ok = p.copy(&out, "second");
		if (!ok || !str_eq(out, "second")) {
			return 2;
		}
	}
	return 0;
}

int test_heap_builtins() {
	int[..] a = { 0 };
	int[..] b = { 0 };
	if (!push(&a, 1) || !push(&a, 2)) {
		return 1;
	}
	if (len(a) != 2 || a[0] != 1 || a[1] != 2) {
		return 2;
	}
	if (!append(&b, a) || len(b) != 2) {
		return 3;
	}
	release(&a);
	if (len(a) != 0) {
		return 4;
	}
	release(&b);
	return 0;
}

int test_pool_vs_heap() {
	pool p {
		int[..] in_pool = { 0 };
		int[..] on_heap = { 0 };
		if (!p.push(&in_pool, 7)) {
			return 1;
		}
		if (!append(&on_heap, in_pool) || len(on_heap) != 1 || on_heap[0] != 7) {
			return 2;
		}
		release(&on_heap);
	}
	return 0;
}

void pool_helper(Pool* p, char[..]* s) {
	(void)p.append(s, "!");
}

int test_pool_ptr_helper() {
	char[..] s = { 0 };
	pool p {
		if (!p.copy(&s, "hi")) {
			return 1;
		}
		pool_helper(&p, &s);
		if (!str_eq(s, "hi!")) {
			return 2;
		}
	}
	return 0;
}

int pool_pkg_run() {
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
	if (test_heap_builtins() != 0) {
		return 6;
	}
	if (test_pool_vs_heap() != 0) {
		return 7;
	}
	if (test_pool_ptr_helper() != 0) {
		return 8;
	}
	return 0;
}
