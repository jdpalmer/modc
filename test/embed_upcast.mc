/* Anonymous embed: pointer upcast (Outer* → Inner*) and value projection (Outer → Inner). */

struct Transform {
	int x;
	int y;
};

struct Entity {
	int id;
	Transform;
};

struct Named {
	int id;
	Transform t;
};

struct Mid {
	Transform;
};

struct Outer {
	int tag;
	Mid;
};

int xform_sum(Transform* t) {
	return t.x + t.y;
}

int xform_sum_val(Transform t) {
	return t.x + t.y;
}

int via_call(void) {
	Entity e = {0};
	e.id = 7;
	e.x = 3;
	e.y = 4;
	return xform_sum(&e) == 7 && e.id == 7;
}

int via_assign(void) {
	Entity e = {0};
	Transform* p = {0};
	e.id = 1;
	e.x = 10;
	e.y = 20;
	p = &e;
	return p.x == 10 && p.y == 20;
}

int via_nested(void) {
	Outer o = {0};
	o.tag = 9;
	o.x = 2;
	o.y = 5;
	return xform_sum(&o) == 7 && o.tag == 9;
}

int named_no_upcast_compiles_field(void) {
	Named n = {0};
	n.id = 1;
	n.t.x = 2;
	n.t.y = 3;
	return xform_sum(&n.t) == 5;
}

int via_value_call(void) {
	Entity e = {0};
	e.id = 7;
	e.x = 3;
	e.y = 4;
	return xform_sum_val(e) == 7 && e.id == 7;
}

int via_value_assign(void) {
	Entity e = {0};
	Transform t = {0};
	e.id = 1;
	e.x = 10;
	e.y = 20;
	t = e;
	return t.x == 10 && t.y == 20;
}

int via_value_nested(void) {
	Outer o = {0};
	o.tag = 9;
	o.x = 2;
	o.y = 5;
	return xform_sum_val(o) == 7 && o.tag == 9;
}

int via_ptr_value(void) {
	Entity e = {0};
	Entity* p = {0};
	e.id = 1;
	e.x = 10;
	e.y = 20;
	p = &e;
	return xform_sum_val(p) == 30 && p.id == 1;
}
