// fail: F:condition requires a scalar expression

struct Flag {
	int value;
};

int bad_aggregate_condition() {
	struct Flag flag = { 1 };
	if (flag) {
		return 1;
	}
	return 0;
}
