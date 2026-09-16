/* Exercise driver -Dname and -Dname=value */

#ifdef CLI_FLAG
int flag_ok() {
	return 1;
}
#else
int flag_ok() {
	return 0;
}
#endif

#ifdef CLI_VALUE
int value_ok() {
	return CLI_VALUE;
}
#else
int value_ok() {
	return 0;
}
#endif
