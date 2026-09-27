// fail: F:__stdcall is for headers only

/* Vendor attributes are header-only. */
int __stdcall bad_stdcall(int x) {
	return x;
}
