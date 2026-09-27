// fail: F:function prototypes are not allowed in %C user code (allowed in headers)

int f();

int main() {
	return f();
}
