/* typedef struct Tag { T *field; } Name — field must not steal the typedef name. */
#pragma once

struct other;

typedef struct tag_ptr_field {
	struct other *p;
	int n;
} tag_ptr_field_t;
