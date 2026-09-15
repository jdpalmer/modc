/* Small exported helper that touches a static local. Auto-inline must not
 * expand this across package boundaries (the $__stN stays package-local). */
char* xinline_empty(void) {
	static char empty[1];
	empty[0] = 0;
	return empty;
}
