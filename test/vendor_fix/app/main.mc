import "engine";
import "ui";

int main(void) {
	return engine_val() + ui_val() == 10 ? 0: 1;
}
