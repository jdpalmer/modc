import "pkg_xinline_lib";

int main() {
	char* s = xinline_empty();
	char[..] room = ranged(s, 1);

	return (s != 0 && room[0] == 0) ? 0 : 1;
}
