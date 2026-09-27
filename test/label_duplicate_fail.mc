// fail: F:duplicate label 'here'

int bad() {
here:
	goto done;
here:
	return 1;
done:
	return 0;
}
