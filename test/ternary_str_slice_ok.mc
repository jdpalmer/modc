/* ok: string-literal ternaries convert to const char[..] */

const char[..] pick_lit(int which) {
	return which ? "yes" : "no";
}

int main() {
	int which = 1;
	const char[..] s = which ? "yes" : "no";
	const char[..] a = "yes";
	const char[..] b = which ? a : "no";
	const char* p = which ? "yes" : "no";

	(void)s;
	(void)b;
	(void)p;
	(void)pick_lit(0);
	return 0;
}
