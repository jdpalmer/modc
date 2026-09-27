#pragma modc c_sources("shim;literal.c")

#include "shim;literal.h"

int main() {
	return shell_literal() == 42 ? 0 : 1;
}
