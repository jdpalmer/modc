/* Shared layout for Win64 / Microsoft x64 ABI interop (modc ↔ host clang). */
#ifndef WIN_ABI_H
#define WIN_ABI_H

typedef struct Pair Pair;
struct Pair {
	int a;
	int b;
};

typedef struct Quad Quad;
struct Quad {
	int a;
	int b;
	int c;
	int d;
};

Pair host_pair(int a, int b);
int host_pair_sum(Pair p);
Pair host_pair_id(Pair p);

Quad host_quad(int a, int b, int c, int d);
int host_quad_sum(Quad q);
Quad host_quad_id(Quad q);

int host_vsum(int n, ...);

#endif
