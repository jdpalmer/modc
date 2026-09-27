// fail: F:static struct fields must be pointers (package-private handles)

struct Bad {
	static int secret;
};
