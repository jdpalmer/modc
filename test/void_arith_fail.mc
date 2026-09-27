// fail: F:`void *` arithmetic is not allowed in %C user code (allowed in headers); cast to a typed pointer first

int bad(void* p) {
	p = p + 1;
	return p != 0;
}
