// fail: F:goto references undefined label 'missing'

int bad() {
	goto missing;
}
