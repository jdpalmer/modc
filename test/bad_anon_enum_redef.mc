/* Second anonymous enum reusing a name is still an error. */
enum {
	DupA = 1
};

enum {
	DupA = 2
};
