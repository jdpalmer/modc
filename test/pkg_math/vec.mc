typedef struct Vec2 {
	float x;
	float y;
}
Vec2;

Vec2 vec2(float x, float y) {
	Vec2 v = {0};
	v.x = x;
	v.y = y;
	return v;
}

static float len2(Vec2 v) {
	return v.x * v.x + v.y * v.y;
}

float vec2_len2(Vec2 v) {
	return len2(v);
}
