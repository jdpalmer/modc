// fail: F:goto enters a different lexical scope

int bad() {
	goto target;
	{
		target: return 0;
	}
}
