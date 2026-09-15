#include <stdio.h>
#include <stdlib.h>

struct Point {
	double x;
	double y;
};

struct Particle {
	/* Plan 9 anonymous embed */
	Point;
	double mass;
};

/* auto-dot through pointers; Particle * upcasts to Point * */
double dist2(Point* p) {
	return p.x * p.x + p.y * p.y;
}

int main(void) {
	Particle* particles = { 0 };
	double system_mass = { 0 };
	particles = malloc(100 * sizeof(Particle));
	defer free(particles);
	auto cloud = ranged(particles, 100);
	system_mass = 0;
	for (auto* p: cloud) {
		p.x = (double)rand() / (double)RAND_MAX;
		p.y = (double)rand() / (double)RAND_MAX;
		p.mass = dist2(p);
		system_mass += p.mass;
	}
	printf("Particle system created with total mass %f.\n", system_mass);
	return 0;
}
