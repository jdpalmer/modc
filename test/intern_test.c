#include "ast.h"

int
main(void)
{
	Compiler c = {0};
	char key[32];
	const char raw[] = {'a', '\0', 'b'};
	char *first, *again, *prefix;
	int i;

	first = str_intern_n(&c, raw, sizeof(raw));
	prefix = str_intern_n(&c, raw, 1);
	if(first == prefix)
		return 1;
	for(i = 0; i < 600; i++) {
		snprintf(key, sizeof(key), "grow-%d", i);
		str_intern(&c, key);
	}
	again = str_intern_n(&c, raw, sizeof(raw));
	if(first != again)
		return 2;
	if(memcmp(again, raw, sizeof(raw)) != 0)
		return 3;
	return 0;
}
