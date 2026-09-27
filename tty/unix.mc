// POSIX tty helpers (package-internal + public bodies).
#ifndef _WIN32
import "fs";
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

enum { TtySavedMax = 8 };

struct TtySaved {
	int fd;
	int valid;
	struct termios saved;
};

static struct TtySaved tty_saved[TtySavedMax];

static WinSize tty_wsz0() {
	WinSize z = { 0 };
	return z;
}

static struct TtySaved* tty_slot(int fd, int create) {
	int i = 0;
	int free_i = -1;
	for (i = 0; i < TtySavedMax; i++) {
		if (tty_saved[i].valid && tty_saved[i].fd == fd) {
			return &tty_saved[i];
		}
		if (!tty_saved[i].valid && free_i < 0) {
			free_i = i;
		}
	}
	if (!create || free_i < 0) {
		return 0;
	}
	tty_saved[free_i].fd = fd;
	tty_saved[free_i].valid = 0;
	return &tty_saved[free_i];
}

bool tty_isatty(File* f) {
	int fd = file_fd(f);
	if (fd < 0) {
		return false;
	}
	return isatty(fd) != 0;
}

bool tty_set_raw(File* f) {
	int fd = file_fd(f);
	struct termios t = { 0 };
	struct TtySaved* s = 0;
	if (fd < 0 || isatty(fd) == 0) {
		return false;
	}
	s = tty_slot(fd, 1);
	if (s == 0) {
		return false;
	}
	if (!s.valid) {
		if (tcgetattr(fd, &s.saved) != 0) {
			return false;
		}
		s.valid = 1;
	}
	t = s.saved;
	cfmakeraw(&t);
	return tcsetattr(fd, TCSANOW, &t) == 0;
}

bool tty_set_cooked(File* f) {
	int fd = file_fd(f);
	struct TtySaved* s = 0;
	if (fd < 0 || isatty(fd) == 0) {
		return false;
	}
	s = tty_slot(fd, 0);
	if (s == 0 || !s.valid) {
		return false;
	}
	if (tcsetattr(fd, TCSANOW, &s.saved) != 0) {
		return false;
	}
	s.valid = 0;
	return true;
}

(WinSize, bool) tty_size(File* f) {
	int fd = file_fd(f);
	struct winsize ws = { 0 };
	WinSize out = { 0 };
	if (fd < 0 || isatty(fd) == 0) {
		return (tty_wsz0(), false);
	}
	if (ioctl(fd, TIOCGWINSZ, &ws) != 0) {
		return (tty_wsz0(), false);
	}
	out.rows = ws.ws_row;
	out.cols = ws.ws_col;
	return (out, true);
}

(File, bool) tty_stdin() {
	return file_from_fd_borrow(STDIN_FILENO);
}

(File, bool) tty_stdout() {
	return file_from_fd_borrow(STDOUT_FILENO);
}

(File, bool) tty_stderr() {
	return file_from_fd_borrow(STDERR_FILENO);
}

void tty_reapply_editor_mode(File* f) {
	(void)f;
}
#endif
