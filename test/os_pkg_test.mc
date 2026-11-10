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
	int64_t a = os_mono_ns();
	int64_t b = os_mono_ns();
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

/* Poll.add_* retains File/Cmd pointers; keep them in static storage for the test. */
static Cmd poll_cmd;
static File poll_out;

int test_cmd_echo() {
	Poll p = { 0 };
	char buf[64] = { 0 };
	Cmd zc = { 0 };
	File zf = { 0 };
	poll_cmd = zc;
	poll_out = zf;
#ifdef _WIN32
	{
		const char[..] args[2] = { 0 };
		args[0] = "/c";
		args[1] = "echo hello-modc";
		poll_cmd.init("cmd.exe", args, 2);
	}
#else
	{
		const char[..] args[1] = { 0 };
		args[0] = "hello-modc";
		poll_cmd.init("echo", args, 1);
	}
#endif
	if (!poll_cmd.stdio(StdIn, IoNull)) {
		poll_cmd.close();
		return 2;
	}
	{
		auto (f, ok) = poll_cmd.pipe(StdOut);
		if (!ok) {
			poll_cmd.close();
			return 3;
		}
		poll_out = f;
	}
	if (!poll_cmd.start()) {
		poll_out.close();
		poll_cmd.close();
		return 4;
	}
	p.init();
	if (!p.add_file(&poll_out, PollIn) || !p.add_cmd(&poll_cmd)) {
		poll_out.close();
		poll_cmd.close();
		p.close();
		return 5;
	}
	{
		int saw = 0;
		int i = 0;
		for (; i < 50; i++) {
			auto (n, ok) = p.wait(200);
			if (!ok) {
				poll_out.close();
				poll_cmd.close();
				p.close();
				return 6;
			}
			if (n > 0) {
				saw = 1;
				break;
			}
		}
		if (!saw) {
			poll_out.close();
			poll_cmd.close();
			p.close();
			return 7;
		}
	}
	{
		auto (n, ok) = poll_out.read(buf);
		if (!ok || n < 10 || !str_starts_with(ranged(buf, n), "hello-modc")) {
			poll_out.close();
			poll_cmd.close();
			p.close();
			return 8;
		}
	}
	poll_out.close();
	{
		auto (code, ok) = poll_cmd.wait();
		if (!ok || code != 0) {
			poll_cmd.close();
			p.close();
			return 9;
		}
	}
	poll_cmd.close();
	p.close();
	return 0;
}

int test_cmd_true() {
	Poll p = { 0 };
	Cmd zc = { 0 };
	poll_cmd = zc;
#ifdef _WIN32
	{
		const char[..] args[2] = { 0 };
		args[0] = "/c";
		args[1] = "exit 0";
		poll_cmd.init("cmd.exe", args, 2);
	}
#else
	{
		const char[..] noargs[1] = { 0 };
		poll_cmd.init("true", noargs, 0);
	}
#endif
	poll_cmd.stdio(StdIn, IoNull);
	poll_cmd.stdio(StdOut, IoNull);
	poll_cmd.stdio(StdErr, IoNull);
	if (!poll_cmd.start()) {
		poll_cmd.close();
		return 2;
	}
	p.init();
	if (!p.add_cmd(&poll_cmd)) {
		poll_cmd.close();
		p.close();
		return 3;
	}
	{
		auto (n, ok) = p.wait(5000);
		if (!ok || n < 1 || p.ready_cmd(0) != &poll_cmd) {
			poll_cmd.close();
			p.close();
			return 4;
		}
	}
	{
		auto (code, ok) = poll_cmd.wait();
		if (!ok || code != 0) {
			poll_cmd.close();
			p.close();
			return 5;
		}
	}
	poll_cmd.close();
	p.close();
	return 0;
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

int main() {
	return os_pkg_run();
}
