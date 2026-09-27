// fail: F:'x' shadows a previous declaration

int bad(int x) {
	{
		int x = { 0 };
		return x;
	}
}
