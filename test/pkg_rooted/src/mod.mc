struct Box {
	int n;
};

int
pkg_rooted_val()
{
	return 11;
}

int
(Box *b).scaled(int k)
{
	return b.n * k;
}
