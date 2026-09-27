// fail: F:cannot project through null pointer to Entity

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

int bad() {
	return take((Entity *)0);
}
