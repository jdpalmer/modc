import "tty";
import "fs";
#include <stddef.h>

int test_stdio_borrow() {
	File in = { 0 };
	File out = { 0 };
	File err = { 0 };
	{
		auto (f, ok) = tty_stdin();
		if (!ok) {
			return 1;
		}
		in = f;
	}
	{
		auto (f, ok) = tty_stdout();
		if (!ok) {
			return 2;
		}
		out = f;
	}
	{
		auto (f, ok) = tty_stderr();
		if (!ok) {
			return 3;
		}
		err = f;
	}
	/* close must not close the process stdio descriptors */
	in.close();
	out.close();
	err.close();
	{
		auto (f, ok) = tty_stdout();
		if (!ok) {
			return 4;
		}
		f.close();
	}
	return 0;
}

int test_isatty_size() {
	File in = { 0 };
	{
		auto (f, ok) = tty_stdin();
		if (!ok) {
			return 1;
		}
		in = f;
	}
	{
		bool is_tty = tty_isatty(&in);
		auto (ws, sok) = tty_size(&in);
		if (is_tty) {
			if (!sok || ws.rows <= 0 || ws.cols <= 0) {
				in.close();
				return 2;
			}
		} else if (sok) {
			in.close();
			return 3;
		}
	}
	in.close();
	return 0;
}

int test_raw_cooked() {
	File in = { 0 };
	{
		auto (f, ok) = tty_stdin();
		if (!ok) {
			return 1;
		}
		in = f;
	}
	if (!tty_isatty(&in)) {
		in.close();
		return 0;
	}
	if (!tty_set_raw(&in)) {
		in.close();
		return 2;
	}
	if (!tty_set_cooked(&in)) {
		in.close();
		return 3;
	}
	in.close();
	return 0;
}

int tty_pkg_run() {
	if (test_stdio_borrow() != 0) {
		return 1;
	}
	if (test_isatty_size() != 0) {
		return 2;
	}
	if (test_raw_cooked() != 0) {
		return 3;
	}
	return 0;
}
