// free/releases on a returning failure branch must not poison the success path.
#include <stdlib.h>
#include <stdio.h>

[[provides]] char* take_or_fail(int ok) {
	char* p = malloc(16);
	if (p == NULL) {
		return NULL;
	}
	if (!ok) {
		free(p);
		return NULL;
	}
	return p;
}

char* stash = NULL;

void keep(char* p) {
	stash = p;
}

int main() {
	char* a = take_or_fail(1);
	if (a == NULL) {
		return 1;
	}
	keep(a);
	free(stash);
	stash = NULL;
	{
		char* failed = take_or_fail(0);

		if (failed != NULL) {
			free(failed);
			return 1;
		}
		free(failed);
	}
	printf("ok\n");
	return 0;
}
