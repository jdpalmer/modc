struct Transform {
	int x;
};

struct Named {
	int id;
	Transform t;
};

int take(Transform* t) {
	return t.x;
}

int bad(void) {
	Named n = { 0 };
	n.id = 1;
	n.t.x = 2;
	return take(&n);
}
