/* Token paste (##) and stringify (#) — Prosser subst rules */

#define STR( x) #x
#define CAT( a, b) a ## b
#define ID( x) x
#define YVAL 42
#define ECHO( x) x
#define EMPTY_PASTE( a) x ## a
#define VA_TAIL( fmt, ...) ( fmt, ## __VA_ARGS__)

int stringify_ident() {
	char* s = { 0 };
	s = STR(hello);
	return s[0] == 'h' && s[1] == 'e' && s[2] == 'l' && s[3] == 'l' && s[4] == 'o' && s[5] == 0;
}

int stringify_no_expand() {
	/* # does not expand the argument first → "ID(y)" not "y" */
	char* s = { 0 };
	s = STR(ID(y));
	return s[0] == 'I' && s[1] == 'D' && s[2] == '(' && s[3] == 'y' && s[4] == ')' && s[5] == 0;
}

int paste_ident() {
	int CAT(fo, o) = 42;
	return foo;
}

int arg_expand() {
	/* Ordinary params expand before insert */
	return ECHO(YVAL);
}

int empty_paste() {
	int EMPTY_PASTE() = 7;
	return x;
}

int va_comma_paste() {
	/* Empty __VA_ARGS__: comma before ## dropped */
	return VA_TAIL(1) == 1;
}
