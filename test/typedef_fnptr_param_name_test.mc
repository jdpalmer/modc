/* Portable test: fnptr typedef param names must not become the typedef name.
 * Regression: typedef void (*Fn)(T *buf) prescanned as typedef "buf", then a
 * parameter named buf was shadowed after locals forced a symbol-tab rebuild
 * (buf == NULL / buf.field failed as if buf were the struct typedef). */
#include <stddef.h>

struct Line {
	int x;
};

struct Buffer {
	Line[..] lines;
};

typedef void (*BufferAfterEditFn)(Buffer *buf, int begin, int end);

static BufferAfterEditFn g_fn;

void buffer_set_after_edit(BufferAfterEditFn fn) {
	g_fn = fn;
}

void (Buffer *buf).touch() {
	(void)buf.lines;
}

/* Free function: locals then use of param named buf (jem buffer_clamp pattern). */
void buffer_clamp_location(Buffer *buf, int *loc) {
	int nlines = 0;

	if (buf == NULL || loc == NULL) {
		return;
	}
	nlines = (int)len(buf.lines);
	if (nlines < 1) {
		*loc = 0;
		return;
	}
	(void)nlines;
}

/* Method: same name clash after a local decl. */
void (Buffer *buf).load_probe() {
	size_t i = 0;

	(void)i;
	(void)buf.lines;
}

int typedef_fnptr_param_name_run() {
	Buffer b = { 0 };
	int loc = 1;

	buffer_set_after_edit(NULL);
	buffer_clamp_location(&b, &loc);
	b.load_probe();
	b.touch();
	return 0;
}

int main() {
	return typedef_fnptr_param_name_run();
}
