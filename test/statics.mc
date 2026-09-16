/* Function-statics and aggregate global initializers */

int garr[3] = {
	1, 2, 3 };
int sparse[3] = {
	1, 0, 3 };

struct P {
	int x;
	int y;
};

struct P gp = {
	10, 20 };

char msg[] = "hi";

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
