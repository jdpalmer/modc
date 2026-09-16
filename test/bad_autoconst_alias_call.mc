void mutate(char* p) {
	if (p) {
		p[0] = 'x';
	}
}

void alias_then_mutate(char* p) {
	char* q = { 0 };
	q = p;
	mutate(q);
}

int main(void) {
	alias_then_mutate("nope");
	return 0;
}
