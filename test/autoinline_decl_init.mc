/* Auto-inline must remap params used inside decl initializers (not only assigns). */

typedef struct Cell {
	int ch;
}
Cell;

typedef struct Screen {
	int rows;
	int cols;
	Cell* cells;
}
Screen;

Cell* cell_at(Screen* s, int row, int col) {
	if (row < 0 || col < 0 || row >= s.rows || col >= s.cols) {
		return 0;
	}
	return &s.cells[row * s.cols + col];
}

void put_cell(Screen* s, int row, int col, int ch) {
	Cell* c = cell_at(s, row, col);
	if (c == 0) {
		return;
	}
	c.ch = ch;
}

int autoinline_decl_init_run() {
	Screen scr = { 0 };
	Cell cells[4] = { 0 };
	int col = { 0 };
	scr.cells = cells;
	scr.rows = 1;
	scr.cols = 4;
	col = 1;
	put_cell(&scr, 0, col, 42);
	return cells[1].ch == 42 ? 0: 1;
}
