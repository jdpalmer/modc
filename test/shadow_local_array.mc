/* Block-scoped locals that reuse a name must get distinct QBE stack slots.
 * A prior bug emitted two `%line.addr =l alloc...` lines; QBE kept the smaller
 * slot and memset of the array smashed the caller frame (jem exit PC=0). */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

bool cond(void) {
	return true;
}

struct Line {
	int x;
};

struct Line g_line;

struct Line* get_line(void) {
	return &g_line;
}

int shadow_local_array_run(void) {
	int keep = 1;
	if (cond()) {
		char line[1024] = { 0 };
		memset(line, 'X', 16);
		if (line[0] != 'X') {
			return 1;
		}
	}
	{
		struct Line* line = get_line();
		if (line == NULL) {
			return 2;
		}
		if (keep != 1) {
			return 3;
		}
	}
	return 0;
}
