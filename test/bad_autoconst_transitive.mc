/* Mutating leaf taints mid and top across fixpoint passes. */
void leaf_write(char* p) {
	if (p) {
		p[0] = 'x';
	}
}

void mid_write(char* p) {
	leaf_write(p);
}

void top_write(char* p) {
	mid_write(p);
}

int main() {
	top_write("nope");
	return 0;
}
