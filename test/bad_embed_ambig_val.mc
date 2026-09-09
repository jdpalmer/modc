struct Transform {
	int x;
};

struct Amb {
	Transform;
	Transform;
};

int take(Transform t) {
	return t.x;
}

int bad(void) {
	Amb a = {0};
	a.x = 1;
	return take(a);
}
