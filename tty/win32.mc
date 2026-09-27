// Win32 tty helpers (package-internal + public bodies).
#ifdef _WIN32
import "fs";
#include <windows.h>
#include <stddef.h>

enum { TtySavedMax = 8 };

struct TtySaved {
	HANDLE h;
	int valid;
	DWORD saved_mode;
	DWORD desired_mode;
	int editor;
};

static struct TtySaved tty_saved[TtySavedMax];
static DWORD tty_saved_in_cp;
static DWORD tty_saved_out_cp;
static int tty_cp_saved;

static WinSize tty_wsz0() {
	WinSize z = { 0 };
	return z;
}

static int tty_is_console(HANDLE h) {
	DWORD mode = 0;
	if (h == 0 || h == INVALID_HANDLE_VALUE) {
		return 0;
	}
	if (GetConsoleMode(h, &mode)) {
		return 1;
	}
	return GetFileType(h) == FILE_TYPE_CHAR;
}

static struct TtySaved* tty_slot(HANDLE h, int create) {
	int i = 0;
	int free_i = -1;
	for (i = 0; i < TtySavedMax; i++) {
		if (tty_saved[i].valid && tty_saved[i].h == h) {
			return &tty_saved[i];
		}
		if (!tty_saved[i].valid && free_i < 0) {
			free_i = i;
		}
	}
	if (!create || free_i < 0) {
		return 0;
	}
	tty_saved[free_i].h = h;
	tty_saved[free_i].valid = 0;
	tty_saved[free_i].editor = 0;
	tty_saved[free_i].desired_mode = 0;
	return &tty_saved[free_i];
}

bool tty_isatty(File* f) {
	HANDLE h = (HANDLE)file_handle(f);
	return tty_is_console(h) != 0;
}

bool tty_set_raw(File* f) {
	HANDLE h = (HANDLE)file_handle(f);
	HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	DWORD out_mode = 0;
	struct TtySaved* s = 0;
	if (!tty_is_console(h)) {
		return false;
	}
	s = tty_slot(h, 1);
	if (s == 0) {
		return false;
	}
	if (!GetConsoleMode(h, &mode)) {
		return false;
	}
	if (!s.valid) {
		s.saved_mode = mode;
		s.valid = 1;
	}
	/* Full editor console mode (jem.go term_windows_console / jem.c term_open). */
	mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
		ENABLE_QUICK_EDIT_MODE);
	mode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT |
		ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (!SetConsoleMode(h, mode)) {
		return false;
	}
	s.desired_mode = mode;
	s.editor = 1;
	if (tty_is_console(hout) && GetConsoleMode(hout, &out_mode)) {
		(void)SetConsoleMode(hout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
	if (!tty_cp_saved) {
		tty_saved_in_cp = GetConsoleCP();
		tty_saved_out_cp = GetConsoleOutputCP();
		tty_cp_saved = 1;
		(void)SetConsoleCP(CP_UTF8);
		(void)SetConsoleOutputCP(CP_UTF8);
	}
	return true;
}

bool tty_set_cooked(File* f) {
	HANDLE h = (HANDLE)file_handle(f);
	struct TtySaved* s = 0;
	if (!tty_is_console(h)) {
		return false;
	}
	s = tty_slot(h, 0);
	if (s == 0 || !s.valid) {
		return false;
	}
	if (!SetConsoleMode(h, s.saved_mode)) {
		return false;
	}
	s.valid = 0;
	s.editor = 0;
	s.desired_mode = 0;
	if (tty_cp_saved) {
		(void)SetConsoleCP(tty_saved_in_cp);
		(void)SetConsoleOutputCP(tty_saved_out_cp);
		tty_cp_saved = 0;
	}
	return true;
}

/* Re-apply editor input mode if Windows Terminal reset it after ANSI output. */
void tty_reapply_editor_mode(File* f) {
	HANDLE h = (HANDLE)file_handle(f);
	struct TtySaved* s = 0;
	DWORD current = 0;
	if (!tty_is_console(h)) {
		return;
	}
	s = tty_slot(h, 0);
	if (s == 0 || !s.valid || !s.editor || s.desired_mode == 0) {
		return;
	}
	if (!GetConsoleMode(h, &current)) {
		return;
	}
	if (current != s.desired_mode) {
		(void)SetConsoleMode(h, s.desired_mode);
	}
}

(WinSize, bool) tty_size(File* f) {
	HANDLE h = (HANDLE)file_handle(f);
	CONSOLE_SCREEN_BUFFER_INFO info = { 0 };
	WinSize out = { 0 };
	if (!tty_is_console(h)) {
		return (tty_wsz0(), false);
	}
	if (!GetConsoleScreenBufferInfo(h, &info)) {
		return (tty_wsz0(), false);
	}
	out.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
	out.cols = info.srWindow.Right - info.srWindow.Left + 1;
	return (out, true);
}

(File, bool) tty_stdin() {
	return file_from_handle_borrow(GetStdHandle(STD_INPUT_HANDLE));
}

(File, bool) tty_stdout() {
	return file_from_handle_borrow(GetStdHandle(STD_OUTPUT_HANDLE));
}

(File, bool) tty_stderr() {
	return file_from_handle_borrow(GetStdHandle(STD_ERROR_HANDLE));
}

/* Console wait: poll GetNumberOfConsoleInputEvents; pipes use PeekNamedPipe. */
bool tty_wait_readable(File* f, int timeout_ms) {
	HANDLE h = (HANDLE)file_handle(f);
	DWORD ft = 0;
	DWORD n = 0;
	DWORD avail = 0;
	DWORD waited = 0;
	DWORD step = 10;
	DWORD timeout = 0;
	if (h == 0 || h == INVALID_HANDLE_VALUE) {
		return false;
	}
	ft = GetFileType(h);
	if (ft == FILE_TYPE_CHAR) {
		if (GetNumberOfConsoleInputEvents(h, &n) && n > 0) {
			return true;
		}
		if (timeout_ms == 0) {
			return false;
		}
		if (timeout_ms < 0) {
			timeout = 24u * 60u * 60u * 1000u;
		} else {
			timeout = timeout_ms;
		}
		while (waited < timeout) {
			Sleep(step);
			waited = waited + step;
			if (GetNumberOfConsoleInputEvents(h, &n) && n > 0) {
				return true;
			}
		}
		return false;
	}
	if (ft == FILE_TYPE_PIPE) {
		if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
			if (avail > 0) {
				return true;
			}
		} else if (GetLastError() == ERROR_BROKEN_PIPE) {
			return true;
		}
		if (timeout_ms == 0) {
			return false;
		}
		if (timeout_ms < 0) {
			timeout = 24u * 60u * 60u * 1000u;
		} else {
			timeout = timeout_ms;
		}
		while (waited < timeout) {
			Sleep(1);
			waited = waited + 1;
			if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
				if (avail > 0) {
					return true;
				}
			} else if (GetLastError() == ERROR_BROKEN_PIPE) {
				return true;
			}
		}
		return false;
	}
	/* Disk / other: let subsequent read decide. */
	return true;
}
#endif
