// fail: F:use of consumed value

import "fs";

int main() {
	File f = { 0 };
	f.close();
	f.close();
	return 0;
}
