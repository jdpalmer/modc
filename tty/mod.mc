// tty — terminal mode and size for File handles. Not a VT parser.
//
// Methods cannot attach to fs.File from this package, so the API is free
// functions (tty_isatty, tty_set_raw, tty_set_cooked, tty_size, tty_stdin /
// stdout / stderr). On Windows, tty_set_raw also enables VT input/mouse and
// UTF-8 code pages; tty_reapply_editor_mode restores input and output modes
// after ANSI output if Windows Terminal reset them; tty_wait_readable polls console events
// or named pipes. Stdio wrappers borrow fds/handles: File.close does not
// close 0/1/2. Compose with fs Writer/Reader via writer_from_file /
// reader_from_file; flush before set_raw/set_cooked.
//
// Bodies live in unix.mc / win32.mc (no prototypes in .mc files).
import "fs";

struct WinSize {
	int rows;
	int cols;
};
