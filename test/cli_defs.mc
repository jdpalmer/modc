/* Exercise driver -Dname and -Dname=value */

#ifdef CLI_FLAG
int flag_ok(void) {
	return 1;
}
#else
int flag_ok(void) {
	return 0;
}
#endif

#ifdef CLI_VALUE
int value_ok(void) {
	return CLI_VALUE;
}
#else
int value_ok(void) {
	return 0;
}
#endif
