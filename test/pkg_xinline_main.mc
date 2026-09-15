import "pkg_xinline_lib";

int main(void) {
	char* s = { 0 };
	s = xinline_empty();
	return (s != 0 && s[0] == 0) ? 0: 1;
}
