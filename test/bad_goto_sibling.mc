int bad(int choose) {
	if (choose) {
		goto target;
	}
	{
		target: return 0;
	}
}
