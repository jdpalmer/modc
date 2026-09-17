int bad() {
	goto target;
	{
		target: return 0;
	}
}
