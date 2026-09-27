import "os";
import "fs";
import "str";
#include <stdint.h>
#include <stddef.h>

int test_env() {
	const char[..] key = "MODC_OS_PKG_KEY";
	os_set_env(key, "");
	{
		auto (ok, v) = os_env(key);
		(void)v;
		if (ok) {
			return 1;
		}
	}
	if (!os_set_env(key, "modc-os")) {
		return 2;
	}
	{
		auto (ok, v) = os_env(key);
		if (!ok || !str_eq(v, "modc-os")) {
			os_set_env(key, "");
			return 3;
		}
	}
	if (!os_set_env(key, "")) {
		return 4;
	}
	{
		auto (ok, v) = os_env(key);
		(void)v;
		if (ok) {
			return 5;
		}
	}
	return 0;
}

int test_cwd() {
	char buf[4096] = { 0 };
	{
		auto (view, ok) = os_cwd(buf);
		if (!ok || len(view) == 0 || buf[0] == 0) {
			return 1;
		}
	}
	{
		char tiny[1] = { 0 };
		tiny[0] = 'x';
		{
			auto (view, ok) = os_cwd(tiny);
			(void)view;
			if (ok) {
				return 2;
			}
		}
	}
	return 0;
}

int test_mono() {
	int64_t a = 0;
	int64_t b = 0;
	a = os_mono_ns();
	b = os_mono_ns();
	if (a <= 0 || b < a) {
		return 1;
	}
	return 0;
}

int test_look_path() {
	char buf[4096] = { 0 };
#ifdef _WIN32
	{
		auto (view, ok) = os_look_path(buf, "cmd.exe");
		(void)view;
		if (!ok) {
			return 1;
		}
	}
#else
	{
		auto (view, ok) = os_look_path(buf, "true");
		if (!ok || len(view) == 0 || view[0] != '/') {
			return 1;
		}
	}
#endif
	return 0;
}

int test_cmd_echo() {
#ifdef _WIN32
	Cmd c = { 0 };
	Poll p = { 0 };
	File out = { 0 };
	char buf[64] = { 0 };
	const char[..] args[2] = { 0 };
	args[0] = "/c";
	args[1] = "echo hello-modc";
	c.init("cmd.exe", args, 2);
	if (!c.stdio(StdIn, IoNull)) {
		c.close();
		return 2;
	}
	{
		auto (f, ok) = c.pipe(StdOut);
		if (!ok) {
			c.close();
			return 3;
		}
		out = f;
	}
	if (!c.start()) {
		out.close();
		c.close();
		return 4;
	}
	p.init();
	if (!p.add_file(&out, PollIn) || !p.add_cmd(&c)) {
		out.close();
		c.close();
		p.close();
		return 5;
	}
	{
		int saw = 0;
		int i = 0;
		for (i = 0; i < 50; i++) {
			auto (n, ok) = p.wait(200);
			if (!ok) {
				out.close();
				c.close();
				p.close();
				return 6;
			}
			if (n > 0) {
				saw = 1;
				break;
			}
		}
		if (!saw) {
			out.close();
			c.close();
			p.close();
			return 7;
		}
	}
	{
		auto (n, ok) = out.read(buf);
		if (!ok || n < 10 || !str_starts_with(ranged(buf, n), "hello-modc")) {
			out.close();
			c.close();
			p.close();
			return 8;
		}
	}
	out.close();
	{
		auto (code, ok) = c.wait();
		if (!ok || code != 0) {
			c.close();
			p.close();
			return 9;
		}
	}
	c.close();
	p.close();
	return 0;
#else
	Cmd c = { 0 };
	Poll p = { 0 };
	File out = { 0 };
	char buf[64] = { 0 };
	const char[..] args[1] = { 0 };
	args[0] = "hello-modc";
	c.init("echo", args, 1);
	if (!c.stdio(StdIn, IoNull)) {
		c.close();
		return 2;
	}
	{
		auto (f, ok) = c.pipe(StdOut);
		if (!ok) {
			c.close();
			return 3;
		}
		out = f;
	}
	if (!c.start()) {
		out.close();
		c.close();
		return 4;
	}
	p.init();
	if (!p.add_file(&out, PollIn) || !p.add_cmd(&c)) {
		out.close();
		c.close();
		p.close();
		return 5;
	}
	{
		int saw = 0;
		int i = 0;
		for (i = 0; i < 50; i++) {
			auto (n, ok) = p.wait(200);
			if (!ok) {
				out.close();
				c.close();
				p.close();
				return 6;
			}
			if (n > 0) {
				saw = 1;
				break;
			}
		}
		if (!saw) {
			out.close();
			c.close();
			p.close();
			return 7;
		}
	}
	{
		auto (n, ok) = out.read(buf);
		if (!ok || n < 10 || !str_starts_with(ranged(buf, n), "hello-modc")) {
			out.close();
			c.close();
			p.close();
			return 8;
		}
	}
	out.close();
	{
		auto (code, ok) = c.wait();
		if (!ok || code != 0) {
			c.close();
			p.close();
			return 9;
		}
	}
	c.close();
	p.close();
	return 0;
#endif
}

int test_cmd_true() {
#ifdef _WIN32
	Cmd c = { 0 };
	Poll p = { 0 };
	const char[..] args[2] = { 0 };
	args[0] = "/c";
	args[1] = "exit 0";
	c.init("cmd.exe", args, 2);
	c.stdio(StdIn, IoNull);
	c.stdio(StdOut, IoNull);
	c.stdio(StdErr, IoNull);
	if (!c.start()) {
		c.close();
		return 2;
	}
	p.init();
	if (!p.add_cmd(&c)) {
		c.close();
		p.close();
		return 3;
	}
	{
		auto (n, ok) = p.wait(5000);
		if (!ok || n < 1 || p.ready_cmd(0) != &c) {
			c.close();
			p.close();
			return 4;
		}
	}
	{
		auto (code, ok) = c.wait();
		if (!ok || code != 0) {
			c.close();
			p.close();
			return 5;
		}
	}
	c.close();
	p.close();
	return 0;
#else
	Cmd c = { 0 };
	Poll p = { 0 };
	const char[..] noargs[1] = { 0 };
	c.init("true", noargs, 0);
	c.stdio(StdIn, IoNull);
	c.stdio(StdOut, IoNull);
	c.stdio(StdErr, IoNull);
	if (!c.start()) {
		c.close();
		return 2;
	}
	p.init();
	if (!p.add_cmd(&c)) {
		c.close();
		p.close();
		return 3;
	}
	{
		auto (n, ok) = p.wait(5000);
		if (!ok || n < 1 || p.ready_cmd(0) != &c) {
			c.close();
			p.close();
			return 4;
		}
	}
	{
		auto (code, ok) = c.wait();
		if (!ok || code != 0) {
			c.close();
			p.close();
			return 5;
		}
	}
	c.close();
	p.close();
	return 0;
#endif
}

int os_pkg_run() {
	if (test_env() != 0) {
		return 1;
	}
	if (test_cwd() != 0) {
		return 2;
	}
	if (test_mono() != 0) {
		return 3;
	}
	if (test_look_path() != 0) {
		return 4;
	}
	if (test_cmd_echo() != 0) {
		return 5;
	}
	if (test_cmd_true() != 0) {
		return 6;
	}
	return 0;
}
