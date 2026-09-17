/* Function-statics and aggregate global initializers */

int garr[3] = {
	1, 2, 3 };
int sparse[3] = {
	1, 0, 3 };
int designated[5] = {
	[3] = 7, 9 };

struct P {
	int x;
	int y;
};

struct P gp = {
	10, 20 };

struct Triple {
	int x;
	int y;
	int z;
};

struct Triple cursor_global = {
	.y = 20, 30 };

char msg[] = "hi";
char padded_msg[8] = "x";
char exact_msg[2] = "hi";
char adjacent_msg[] = "poison";

int bump() {
	static int n;
	n = n + 1;
	return n;
}

int bump_from() {
	static int n = 5;
	n = n + 1;
	return n;
}

int static_arr_sum() {
	static int a[3] = {
		4, 5, 6 };
	return a[0] + a[1] + a[2];
}

int arr_at(int i) {
	return garr[i];
}

int sparse_at(int i) {
	return sparse[i];
}

int gpx() {
	return gp.x;
}

int gpy() {
	return gp.y;
}

int msg_at(int i) {
	return msg[i];
}

int designated_global_ok() {
	return designated[0] == 0 && designated[3] == 7 && designated[4] == 9;
}

int designated_local_ok() {
	int a[5] = {
		[3] = 7, 9 };
	return a[0] == 0 && a[3] == 7 && a[4] == 9;
}

int designated_inferred_ok() {
	int a[] = {
		[3] = 7, 9 };
	return len(a) == 5 && a[3] == 7 && a[4] == 9;
}

int field_cursor_ok() {
	struct Triple v = {
		.y = 2, 3 };
	return cursor_global.x == 0 && cursor_global.y == 20 &&
	       cursor_global.z == 30 && v.x == 0 && v.y == 2 && v.z == 3;
}

int string_array_init_ok() {
	char local[8] = "y";
	char adjacent[] = "noise";
	int i = 0;

	if (padded_msg[0] != 'x' || exact_msg[0] != 'h' ||
	    exact_msg[1] != 'i' || adjacent_msg[0] != 'p') {
		return 0;
	}
	for (i = 1; i < 8; i++) {
		if (padded_msg[i] != 0 || local[i] != 0) {
			return 0;
		}
	}
	return local[0] == 'y' && adjacent[0] == 'n';
}
