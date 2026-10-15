/* Intentional panic; must exit non-zero with a message. */
import "crash";

int main() {
	panic("test-panic");
	return 0;
}
