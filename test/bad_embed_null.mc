struct Transform {
	int x;
};

struct Entity {
	int id;
	Transform;
};

int take(Transform t) {
	return t.x;
}

int bad(void) {
	return take((Entity *)0);
}
