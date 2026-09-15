/* Anonymous enums must not be re-parsed as redefinitions afterprescan. */
enum {
	AnonA = 1,
	AnonB = 2
};

enum { AnonC = 3 };

int anon_enum_run(void) {
	if (AnonA + AnonB + AnonC != 6) {
		return 1;
	}
	return 0;
}
