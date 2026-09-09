/* Headers may read locals without definite assignment. */

int
hdr_uninit(void)
{
	int x;

	return x;
}
