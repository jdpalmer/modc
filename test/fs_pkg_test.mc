import "fs";
import "str";
#include <stddef.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int write_all(File* f, const char[..] src) {
	size_t off = 0;
	while (off < len(src)) {
		auto (n, ok) = f.write(src[off ..]);
		if (!ok || n <= 0) {
			return 0;
		}
		off += n;
	}
	return 1;
}

int test_rw_seek_remove() {
	const char[..] name = "fs_pkg_tmp.bin";
	char got[16] = { 0 };
	File f = { 0 };
	int ok = 0;
	fs_remove(name);
	{
		auto (opened, good) = file_open(name, OpenRead | OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok) {
		return 1;
	}
	if (!write_all(&f, "hello")) {
		f.close();
		fs_remove(name);
		return 2;
	}
	{
		auto (pos, sok) = f.seek(0, SeekSet);
		if (!sok || pos != 0) {
			f.close();
			fs_remove(name);
			return 3;
		}
	}
	{
		auto (n, rok) = f.read(got);
		if (!rok || n != 5 || !str_eq(ranged(got, 5), "hello")) {
			f.close();
			fs_remove(name);
			return 4;
		}
	}
	{
		auto (n, rok) = f.read(got);
		if (!rok || n != 0) {
			f.close();
			fs_remove(name);
			return 5;
		}
	}
	f.close();
	if (!fs_remove(name)) {
		return 6;
	}
	return 0;
}

int test_append() {
	const char[..] name = "fs_pkg_tmp.bin";
	char got[16] = { 0 };
	File f = { 0 };
	int ok = 0;
	fs_remove(name);
	{
		auto (opened, good) = file_open(name, OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok || !write_all(&f, "ab")) {
		f.close();
		fs_remove(name);
		return 1;
	}
	f.close();
	{
		auto (opened, good) = file_open(name, OpenWrite | OpenCreate | OpenAppend);
		f = opened;
		ok = good;
	}
	if (!ok || !write_all(&f, "cd")) {
		f.close();
		fs_remove(name);
		return 2;
	}
	f.close();
	{
		auto (opened, good) = file_open(name, OpenRead);
		f = opened;
		ok = good;
	}
	if (!ok) {
		fs_remove(name);
		return 3;
	}
	{
		auto (n, rok) = f.read(got);
		if (!rok || n != 4 || !str_eq(ranged(got, 4), "abcd")) {
			f.close();
			fs_remove(name);
			return 4;
		}
	}
	f.close();
	if (!fs_remove(name)) {
		return 5;
	}
	return 0;
}

int test_closed() {
	char buf[8] = { 0 };
	File f = { 0 };
	{
		auto (n, ok) = f.read(buf);
		if (ok || n != 0) {
			return 1;
		}
	}
	{
		auto (n, ok) = f.write("x");
		if (ok || n != 0) {
			return 2;
		}
	}
	{
		auto (n, ok) = f.seek(0, SeekSet);
		if (ok || n != 0) {
			return 3;
		}
	}
	f.close();
	return 0;
}

int test_mkdir_rename() {
	const char[..] dir = "fs_pkg_tmp_dir";
	const char[..] from = "fs_pkg_tmp.bin";
	const char[..] to = "fs_pkg_tmp_renamed.bin";
	File f = { 0 };
	int ok = 0;
	fs_remove(from);
	fs_remove(to);
	fs_remove(dir);
	if (!fs_mkdir(dir)) {
		return 1;
	}
	if (!fs_remove(dir)) {
		return 2;
	}
	{
		auto (opened, good) = file_open(from, OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok) {
		return 3;
	}
	f.close();
	if (!fs_rename(from, to)) {
		fs_remove(from);
		return 4;
	}
	if (!fs_remove(to)) {
		return 5;
	}
	return 0;
}

int test_stat_dir() {
	const char[..] dir = "fs_pkg_list";
	const char[..] fa = "fs_pkg_list/a";
	const char[..] fb = "fs_pkg_list/b";
	const char[..] file = "fs_pkg_stat.bin";
	File f = { 0 };
	Dir d = { 0 };
	int ok = 0;
	int saw_a = 0;
	int saw_b = 0;
	int nent = 0;
	fs_remove(fa);
	fs_remove(fb);
	fs_remove(dir);
	fs_remove(file);
	if (!fs_mkdir(dir)) {
		return 1;
	}
	{
		auto (info, sok) = fs_stat(dir);
		if (!sok || info.kind != KindDir || info.size != 0) {
			fs_remove(dir);
			return 2;
		}
	}
	{
		auto (opened, good) = file_open(fa, OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok || !write_all(&f, "hello")) {
		f.close();
		fs_remove(fa);
		fs_remove(dir);
		return 3;
	}
	f.close();
	{
		auto (opened, good) = file_open(fb, OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok || !write_all(&f, "x")) {
		f.close();
		fs_remove(fa);
		fs_remove(fb);
		fs_remove(dir);
		return 4;
	}
	f.close();
	{
		auto (info, sok) = fs_stat(fa);
		if (!sok || info.kind != KindFile || info.size != 5 || info.mtime_ns <= 0) {
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 5;
		}
	}
	{
		auto (opened, good) = dir_open(dir);
		d = opened;
		ok = good;
	}
	if (!ok) {
		fs_remove(fa);
		fs_remove(fb);
		fs_remove(dir);
		return 6;
	}
	for (;;) {
		auto (name, nok) = d.next();
		if (!nok) {
			d.close();
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 7;
		}
		if (str_is_empty(name)) {
			break;
		}
		if (str_eq(name, ".") || str_eq(name, "..")) {
			d.close();
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 8;
		}
		if (str_eq(name, "a")) {
			saw_a = 1;
		} else if (str_eq(name, "b")) {
			saw_b = 1;
		} else {
			d.close();
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 9;
		}
		nent = nent + 1;
		if (nent > 4) {
			d.close();
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 10;
		}
	}
	d.close();
	if (!saw_a || !saw_b) {
		fs_remove(fa);
		fs_remove(fb);
		fs_remove(dir);
		return 11;
	}
	{
		auto (name, nok) = d.next();
		(void)name;
		if (nok) {
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 12;
		}
	}
	{
		auto (opened, good) = file_open(file, OpenWrite | OpenCreate | OpenTrunc);
		f = opened;
		ok = good;
	}
	if (!ok) {
		fs_remove(fa);
		fs_remove(fb);
		fs_remove(dir);
		return 13;
	}
	f.close();
	{
		auto (opened, good) = dir_open(file);
		(void)opened;
		if (good) {
			fs_remove(file);
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 14;
		}
	}
	{
		auto (info, sok) = fs_stat("fs_pkg_missing");
		if (sok || info.kind != 0) {
			fs_remove(file);
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 15;
		}
	}
	{
#ifdef _WIN32
		auto (info, sok) = fs_stat("NUL");
#else
		auto (info, sok) = fs_stat("/dev/null");
#endif
		if (!sok || info.kind != KindOther) {
			fs_remove(file);
			fs_remove(fa);
			fs_remove(fb);
			fs_remove(dir);
			return 16;
		}
	}
	if (!fs_remove(file) || !fs_remove(fa) || !fs_remove(fb) || !fs_remove(dir)) {
		return 17;
	}
	return 0;
}

int test_from_fd_pipe() {
#ifdef _WIN32
	return 0;
#else
	int pfd[2] = { 0 };
	File r = { 0 };
	File w = { 0 };
	char got[8] = { 0 };
	if (pipe(pfd) != 0) {
		return 1;
	}
	{
		auto (rf, rok) = file_from_fd(pfd[0]);
		auto (wf, wok) = file_from_fd(pfd[1]);
		if (!rok || !wok) {
			if (rok) {
				rf.close();
			} else {
				close(pfd[0]);
			}
			if (wok) {
				wf.close();
			} else {
				close(pfd[1]);
			}
			return 2;
		}
		r = rf;
		w = wf;
	}
	if (file_fd(&r) != pfd[0] || file_fd(&w) != pfd[1]) {
		r.close();
		w.close();
		return 3;
	}
	if (!write_all(&w, "xy")) {
		r.close();
		w.close();
		return 4;
	}
	w.close();
	{
		auto (n, rok) = r.read(got);
		if (!rok || n != 2 || got[0] != 'x' || got[1] != 'y') {
			r.close();
			return 5;
		}
	}
	{
		auto (pos, sok) = r.seek(0, SeekSet);
		(void)pos;
		if (sok) {
			r.close();
			return 6;
		}
	}
	{
		auto (n, rok) = r.read(got);
		if (!rok || n != 0) {
			r.close();
			return 7;
		}
	}
	r.close();
	return 0;
#endif
}

int test_buf_open_roundtrip() {
	const char[..] name = "fs_pkg_buf.bin";
	char got[32] = { 0 };
	Writer w = { 0 };
	Reader r = { 0 };
	fs_remove(name);
	{
		auto (ow, ok) = writer_open(name, OpenWrite | OpenCreate | OpenTrunc, 0);
		if (!ok) {
			return 1;
		}
		w = ow;
	}
	{
		auto (n, ok) = w.write("hello buf");
		if (!ok || n != 9) {
			w.close();
			fs_remove(name);
			return 2;
		}
	}
	w.close();
	{
		auto (rd, ok) = reader_open(name, OpenRead, 0);
		if (!ok) {
			fs_remove(name);
			return 3;
		}
		r = rd;
	}
	{
		auto (n, ok) = r.read(got);
		if (!ok || n != 9 || got[0] != 'h' || got[8] != 'f') {
			r.close();
			fs_remove(name);
			return 4;
		}
	}
	{
		auto (n, ok) = r.read(got);
		if (!ok || n != 0) {
			r.close();
			fs_remove(name);
			return 5;
		}
	}
	r.close();
	if (!fs_remove(name)) {
		return 6;
	}
	return 0;
}

int test_buf_tiny_writes() {
	const char[..] name = "fs_pkg_buf_tiny.bin";
	char got[16] = { 0 };
	Writer w = { 0 };
	Reader r = { 0 };
	int i = 0;
	fs_remove(name);
	{
		auto (ow, ok) = writer_open(name, OpenWrite | OpenCreate | OpenTrunc, 8);
		if (!ok) {
			return 1;
		}
		w = ow;
	}
	for (i = 0; i < 10; i = i + 1) {
		char one[1] = { 0 };
		one[0] = (char)('0' + i);
		auto (n, ok) = w.write(one);
		if (!ok || n != 1) {
			w.close();
			fs_remove(name);
			return 2;
		}
	}
	w.close();
	{
		auto (rd, ok) = reader_open(name, OpenRead, 4);
		if (!ok) {
			fs_remove(name);
			return 3;
		}
		r = rd;
	}
	{
		auto (n, ok) = r.read(got);
		if (!ok || n != 10) {
			r.close();
			fs_remove(name);
			return 4;
		}
		for (i = 0; i < 10; i = i + 1) {
			if (got[i] != (char)('0' + i)) {
				r.close();
				fs_remove(name);
				return 5;
			}
		}
	}
	r.close();
	fs_remove(name);
	return 0;
}

int test_buf_utf8_split() {
	const char[..] name = "fs_pkg_buf_utf8.bin";
	char euro[3] = {
		0xe2, 0x82, 0xac };
	char got[8] = { 0 };
	Writer w = { 0 };
	File f = { 0 };
	int i = 0;
	fs_remove(name);
	{
		auto (ow, ok) = writer_open(name, OpenWrite | OpenCreate | OpenTrunc, 4);
		if (!ok) {
			return 1;
		}
		w = ow;
	}
	for (i = 0; i < 3; i = i + 1) {
		char one[1] = { 0 };
		one[0] = euro[i];
		auto (n, ok) = w.write(one);
		if (!ok || n != 1) {
			w.close();
			fs_remove(name);
			return 2;
		}
		if (!w.flush()) {
			w.close();
			fs_remove(name);
			return 3;
		}
	}
	w.close();
	{
		auto (opened, ok) = file_open(name, OpenRead);
		if (!ok) {
			fs_remove(name);
			return 4;
		}
		f = opened;
	}
	{
		auto (n, rok) = f.read(got);
		f.close();
		if (!rok || n != 3 || got[0] != euro[0] || got[1] != euro[1] || got[2] != euro[2]) {
			fs_remove(name);
			return 5;
		}
	}
	fs_remove(name);
	return 0;
}

int test_buf_borrow_init() {
	const char[..] name = "fs_pkg_buf_borrow.bin";
	char got[8] = { 0 };
	File f = { 0 };
	Writer w = { 0 };
	Reader r = { 0 };
	fs_remove(name);
	{
		auto (opened, ok) = file_open(name, OpenRead | OpenWrite | OpenCreate | OpenTrunc);
		if (!ok) {
			return 1;
		}
		f = opened;
	}
	{
		auto (ow, ok) = writer_from_file(&f, 8);
		if (!ok) {
			f.close();
			fs_remove(name);
			return 2;
		}
		w = ow;
	}
	{
		auto (n, ok) = w.write("ab");
		if (!ok || n != 2) {
			w.close();
			f.close();
			fs_remove(name);
			return 3;
		}
	}
	w.close();
	{
		auto (pos, sok) = f.seek(0, SeekSet);
		(void)pos;
		if (!sok) {
			f.close();
			fs_remove(name);
			return 4;
		}
	}
	{
		auto (rd, ok) = reader_from_file(&f, 8);
		if (!ok) {
			f.close();
			fs_remove(name);
			return 5;
		}
		r = rd;
	}
	{
		auto (n, ok) = r.read(got);
		if (!ok || n != 2 || got[0] != 'a' || got[1] != 'b') {
			r.close();
			f.close();
			fs_remove(name);
			return 6;
		}
	}
	r.close();
	{
		auto (pos, sok) = f.seek(0, SeekSet);
		(void)pos;
		if (!sok) {
			f.close();
			fs_remove(name);
			return 7;
		}
	}
	{
		auto (n, rok) = f.read(got);
		if (!rok || n != 2 || got[0] != 'a' || got[1] != 'b') {
			f.close();
			fs_remove(name);
			return 8;
		}
	}
	f.close();
	fs_remove(name);
	return 0;
}

int fs_pkg_run() {
	if (test_rw_seek_remove() != 0) {
		return 1;
	}
	if (test_append() != 0) {
		return 2;
	}
	if (test_closed() != 0) {
		return 3;
	}
	if (test_mkdir_rename() != 0) {
		return 4;
	}
	if (test_stat_dir() != 0) {
		return 5;
	}
	if (test_from_fd_pipe() != 0) {
		return 6;
	}
	if (test_buf_open_roundtrip() != 0) {
		return 7;
	}
	if (test_buf_tiny_writes() != 0) {
		return 8;
	}
	if (test_buf_utf8_split() != 0) {
		return 9;
	}
	if (test_buf_borrow_init() != 0) {
		return 10;
	}
	return 0;
}

int main() {
	return fs_pkg_run();
}
