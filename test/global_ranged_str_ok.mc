/* File-scope const char[..] from string literals (typecheck). */
const char[..] g_one = "one";
const char[..] g_keywords[] = {
	"one", "two", "three" };
static const char[..] g_kw3[3] = {
	"a", "bb", "ccc" };

int global_ranged_str_ok() {
	return (int)(len(g_one) + len(g_keywords) + len(g_kw3[2]));
}
