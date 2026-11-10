// fail: F:excess elements in

typedef struct {
	int x;
} TagEntry;

int main() {
	TagEntry e = (TagEntry){ 1, 2 };
	return e.x;
}
