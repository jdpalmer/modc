#include <assert.h>

const char[..] g_one = "one";
const char[..] g_keywords[] = {
	"one", "two", "three" };
static const char[..] g_kw3[3] = {
	"a", "bb", "ccc" };

int main() {
	assert(len(g_one) == 3);
	assert(g_one[0] == 'o');
	assert(len(g_keywords) == 3);
	assert(len(g_keywords[0]) == 3);
	assert(len(g_keywords[1]) == 3);
	assert(len(g_keywords[2]) == 5);
	assert(g_keywords[1][0] == 't');
	assert(g_keywords[2][4] == 'e');
	assert(len(g_kw3[0]) == 1);
	assert(len(g_kw3[1]) == 2);
	assert(len(g_kw3[2]) == 3);
	return 0;
}
