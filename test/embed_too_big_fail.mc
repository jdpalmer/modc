// fail: F:embedded resource is too large
static char a[2] = {
#embed "embed_data/abc.bin"
};
