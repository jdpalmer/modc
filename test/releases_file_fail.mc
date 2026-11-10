// fail: F:use of released value

import "fs";

int main() {
	File f = { 0 };
	f.close();
	f.close();
	return 0;
}
