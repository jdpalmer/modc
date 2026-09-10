/* Aggregates nested in arrays must be defined before the outer QBE type. */

typedef struct UndoRecord {
	int kind;
} UndoRecord;

typedef struct UndoGroup {
	UndoRecord items[2];
	int n;
} UndoGroup;

int nest_aggr_arr_run(void) {
	UndoGroup g = {0};

	g.n = 3;
	g.items[0].kind = 1;
	g.items[1].kind = 2;
	return g.n + g.items[0].kind + g.items[1].kind == 6 ? 0 : 1;
}
