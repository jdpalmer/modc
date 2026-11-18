// POSIX tty helpers (package-internal + public bodies).
#ifndef _WIN32
import "fs";
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static enum { TtySavedMax = 8 };

static struct TtySaved {
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
	for (; i < TtySavedMax; i++) {
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

// True if f is an open terminal.
bool tty_isatty(File* f) {
	int fd = file_fd(f);
	if (fd < 0) {
		return false;
	}
	return isatty(fd) != 0;
}

// Save the terminal mode and enter raw mode; false if f is not a terminal.
bool tty_set_raw(File* f) {
	int fd = file_fd(f);
	if (fd < 0 || isatty(fd) == 0) {
		return false;
	}
	struct TtySaved* s = tty_slot(fd, 1);
	if (s == 0) {
		return false;
	}
	if (!s.valid) {
		if (tcgetattr(fd, &s.saved) != 0) {
			return false;
		}
		s.valid = 1;
	}
	struct termios t = s.saved;
	cfmakeraw(&t);
	return tcsetattr(fd, TCSANOW, &t) == 0;
}

// Restore the mode saved by tty_set_raw; false if none was saved.
bool tty_set_cooked(File* f) {
	int fd = file_fd(f);
	if (fd < 0 || isatty(fd) == 0) {
		return false;
	}
	struct TtySaved* s = tty_slot(fd, 0);
	if (s == 0 || !s.valid) {
		return false;
	}
	if (tcsetattr(fd, TCSANOW, &s.saved) != 0) {
		return false;
	}
	s.valid = 0;
	return true;
}

// Terminal rows and columns; false if f is not a terminal.
(WinSize, bool) tty_size(File* f) {
	int fd = file_fd(f);
	struct winsize ws = { 0 };
	if (fd < 0 || isatty(fd) == 0) {
		return (tty_wsz0(), false);
	}
	if (ioctl(fd, TIOCGWINSZ, &ws) != 0) {
		return (tty_wsz0(), false);
	}
	WinSize out = { ws.ws_row, ws.ws_col };
	return (out, true);
}

// Borrowed File for standard input; close does not close fd 0.
[[provides]] (File, bool) tty_stdin() {
	return file_from_fd_borrow(STDIN_FILENO);
}

// Borrowed File for standard output; close does not close fd 1.
[[provides]] (File, bool) tty_stdout() {
	return file_from_fd_borrow(STDOUT_FILENO);
}

// Borrowed File for standard error; close does not close fd 2.
[[provides]] (File, bool) tty_stderr() {
	return file_from_fd_borrow(STDERR_FILENO);
}

// Re-apply terminal modes after ANSI output; no-op on POSIX.
void tty_reapply_editor_mode(File* f) {
	(void)f;
}
#endif
