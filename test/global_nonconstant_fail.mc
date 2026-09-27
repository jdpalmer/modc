// fail: F:global initializer is not a constant expression

int seven() {
	return 7;
}

int bad = seven();
